// LinOSD - hook OpenGL via LD_PRELOAD.
//
// Intercepta glXSwapBuffers, eglSwapBuffers(+WithDamage) tanto por ligação
// direta quanto por glXGetProcAddress/eglGetProcAddress e dlsym() (SDL e
// muitos motores carregam libGL com dlopen e resolvem símbolos com dlsym).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>

#include <atomic>
#include <cstring>

#include "frame_recorder.h"

#define LINOSD_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

using DlsymFn = void* (*)(void*, const char*);
using GlxSwapFn = void (*)(void*, unsigned long);
using EglSwapFn = unsigned int (*)(void*, void*);
using EglSwapDamageFn = unsigned int (*)(void*, void*, const int*, int);
using GetProcFn = void* (*)(const unsigned char*);
using EglGetProcFn = void* (*)(const char*);

DlsymFn realDlsym() {
    static DlsymFn fn = nullptr;
    if (!fn) {
        // dlvsym não é interceptado; tentamos as versões conhecidas do glibc.
        const char* versions[] = {"GLIBC_2.34", "GLIBC_2.2.5", "GLIBC_2.0", "GLIBC_2.17"};
        for (const char* v : versions) {
            fn = reinterpret_cast<DlsymFn>(dlvsym(RTLD_NEXT, "dlsym", v));
            if (fn) break;
        }
    }
    return fn;
}

std::atomic<GlxSwapFn> g_glxSwap{nullptr};
std::atomic<EglSwapFn> g_eglSwap{nullptr};
std::atomic<EglSwapDamageFn> g_eglSwapDamageKHR{nullptr};
std::atomic<EglSwapDamageFn> g_eglSwapDamageEXT{nullptr};
std::atomic<GetProcFn> g_glxGetProc{nullptr};
std::atomic<GetProcFn> g_glxGetProcARB{nullptr};
std::atomic<EglGetProcFn> g_eglGetProc{nullptr};

// Carrega o símbolo real: primeiro RTLD_NEXT, depois as bibliotecas do sistema.
template <typename T>
T loadReal(std::atomic<T>& slot, const char* name, const char* lib) {
    T fn = slot.load(std::memory_order_acquire);
    if (fn) return fn;
    DlsymFn ds = realDlsym();
    if (!ds) return nullptr;
    fn = reinterpret_cast<T>(ds(RTLD_NEXT, name));
    if (!fn && lib) {
        void* h = dlopen(lib, RTLD_LAZY | RTLD_NOLOAD);
        if (!h) h = dlopen(lib, RTLD_LAZY);
        if (h) fn = reinterpret_cast<T>(ds(h, name));
    }
    if (fn) slot.store(fn, std::memory_order_release);
    return fn;
}

void* hookFor(const char* name);

}  // namespace

// ---------------------------------------------------------------------------
// GLX
// ---------------------------------------------------------------------------
LINOSD_EXPORT void glXSwapBuffers(void* dpy, unsigned long drawable) {
    GlxSwapFn real = loadReal(g_glxSwap, "glXSwapBuffers", "libGL.so.1");
    linosd::onPresent(linosd::Api::OpenGL);
    if (real) real(dpy, drawable);
}

LINOSD_EXPORT void* glXGetProcAddress(const unsigned char* name) {
    if (void* h = hookFor(reinterpret_cast<const char*>(name))) return h;
    GetProcFn real = loadReal(g_glxGetProc, "glXGetProcAddress", "libGL.so.1");
    return real ? real(name) : nullptr;
}

LINOSD_EXPORT void* glXGetProcAddressARB(const unsigned char* name) {
    if (void* h = hookFor(reinterpret_cast<const char*>(name))) return h;
    GetProcFn real = loadReal(g_glxGetProcARB, "glXGetProcAddressARB", "libGL.so.1");
    return real ? real(name) : nullptr;
}

// ---------------------------------------------------------------------------
// EGL
// ---------------------------------------------------------------------------
LINOSD_EXPORT unsigned int eglSwapBuffers(void* dpy, void* surface) {
    EglSwapFn real = loadReal(g_eglSwap, "eglSwapBuffers", "libEGL.so.1");
    linosd::onPresent(linosd::Api::EGL);
    return real ? real(dpy, surface) : 0;
}

LINOSD_EXPORT unsigned int eglSwapBuffersWithDamageKHR(void* dpy, void* surface, const int* rects, int n) {
    EglSwapDamageFn real = g_eglSwapDamageKHR.load();
    if (!real) {
        EglGetProcFn gp = loadReal(g_eglGetProc, "eglGetProcAddress", "libEGL.so.1");
        if (gp) real = reinterpret_cast<EglSwapDamageFn>(gp("eglSwapBuffersWithDamageKHR"));
        if (real) g_eglSwapDamageKHR.store(real);
    }
    linosd::onPresent(linosd::Api::EGL);
    return real ? real(dpy, surface, rects, n) : 0;
}

LINOSD_EXPORT unsigned int eglSwapBuffersWithDamageEXT(void* dpy, void* surface, const int* rects, int n) {
    EglSwapDamageFn real = g_eglSwapDamageEXT.load();
    if (!real) {
        EglGetProcFn gp = loadReal(g_eglGetProc, "eglGetProcAddress", "libEGL.so.1");
        if (gp) real = reinterpret_cast<EglSwapDamageFn>(gp("eglSwapBuffersWithDamageEXT"));
        if (real) g_eglSwapDamageEXT.store(real);
    }
    linosd::onPresent(linosd::Api::EGL);
    return real ? real(dpy, surface, rects, n) : 0;
}

LINOSD_EXPORT void* eglGetProcAddress(const char* name) {
    EglGetProcFn real = loadReal(g_eglGetProc, "eglGetProcAddress", "libEGL.so.1");
    if (name && (!std::strcmp(name, "eglSwapBuffersWithDamageKHR") ||
                 !std::strcmp(name, "eglSwapBuffersWithDamageEXT"))) {
        // Só devolve o hook se o driver suporta a extensão.
        if (!real || !real(name)) return nullptr;
    }
    if (void* h = hookFor(name)) return h;
    return real ? real(name) : nullptr;
}

// ---------------------------------------------------------------------------
// dlsym
// ---------------------------------------------------------------------------
LINOSD_EXPORT void* dlsym(void* handle, const char* name) {
    DlsymFn real = realDlsym();
    if (!real) return nullptr;
    if (!linosd::hookDisabled()) {
        if (void* h = hookFor(name)) {
            // Guarda o ponteiro real resolvido a partir do handle do aplicativo.
            void* r = real(handle, name);
            if (!r) return nullptr;
            if (!std::strcmp(name, "glXSwapBuffers")) g_glxSwap.store(reinterpret_cast<GlxSwapFn>(r));
            else if (!std::strcmp(name, "eglSwapBuffers")) g_eglSwap.store(reinterpret_cast<EglSwapFn>(r));
            else if (!std::strcmp(name, "glXGetProcAddress")) g_glxGetProc.store(reinterpret_cast<GetProcFn>(r));
            else if (!std::strcmp(name, "glXGetProcAddressARB")) g_glxGetProcARB.store(reinterpret_cast<GetProcFn>(r));
            else if (!std::strcmp(name, "eglGetProcAddress")) g_eglGetProc.store(reinterpret_cast<EglGetProcFn>(r));
            else if (!std::strcmp(name, "eglSwapBuffersWithDamageKHR"))
                g_eglSwapDamageKHR.store(reinterpret_cast<EglSwapDamageFn>(r));
            else if (!std::strcmp(name, "eglSwapBuffersWithDamageEXT"))
                g_eglSwapDamageEXT.store(reinterpret_cast<EglSwapDamageFn>(r));
            return h;
        }
    }
    return real(handle, name);
}

namespace {

void* hookFor(const char* name) {
    if (!name || linosd::hookDisabled()) return nullptr;
    if (!std::strcmp(name, "glXSwapBuffers")) return reinterpret_cast<void*>(&glXSwapBuffers);
    if (!std::strcmp(name, "glXGetProcAddress")) return reinterpret_cast<void*>(&glXGetProcAddress);
    if (!std::strcmp(name, "glXGetProcAddressARB")) return reinterpret_cast<void*>(&glXGetProcAddressARB);
    if (!std::strcmp(name, "eglSwapBuffers")) return reinterpret_cast<void*>(&eglSwapBuffers);
    if (!std::strcmp(name, "eglGetProcAddress")) return reinterpret_cast<void*>(&eglGetProcAddress);
    if (!std::strcmp(name, "eglSwapBuffersWithDamageKHR"))
        return reinterpret_cast<void*>(&eglSwapBuffersWithDamageKHR);
    if (!std::strcmp(name, "eglSwapBuffersWithDamageEXT"))
        return reinterpret_cast<void*>(&eglSwapBuffersWithDamageEXT);
    return nullptr;
}

}  // namespace
