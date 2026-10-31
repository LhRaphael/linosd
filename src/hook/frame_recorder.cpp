#include "frame_recorder.h"

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace linosd {
namespace {

ShmHeader* g_shm = nullptr;
bool g_failed = false;
std::mutex g_initMutex;
char g_shmName[64] = {0};
uint64_t g_lastPresent = 0;  // para o limitador (antes de dormir)

uint64_t nowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

void readProcessName(char* out, size_t outSize) {
    out[0] = '\0';
    // argv[0] completo (o comm é truncado em 15 caracteres).
    FILE* f = std::fopen("/proc/self/cmdline", "rb");
    if (f) {
        char buf[512];
        size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        buf[n] = '\0';
        const char* base = buf;
        for (const char* p = buf; *p; ++p)
            if (*p == '/' || *p == '\\') base = p + 1;  // '\\' para caminhos do Wine
        std::snprintf(out, outSize, "%.63s", base);
    }
    if (out[0] == '\0') {
        f = std::fopen("/proc/self/comm", "rb");
        if (f) {
            size_t n = std::fread(out, 1, outSize - 1, f);
            std::fclose(f);
            out[n] = '\0';
            char* nl = std::strchr(out, '\n');
            if (nl) *nl = '\0';
        }
    }
}

void cleanup() {
    if (g_shmName[0]) shm_unlink(g_shmName);
}

// Após fork() o filho não deve escrever no segmento do pai.
void atforkChild() {
    g_shm = nullptr;
    g_failed = false;
    g_shmName[0] = '\0';
    g_lastPresent = 0;
}

struct AtforkRegistrar {
    AtforkRegistrar() { pthread_atfork(nullptr, nullptr, atforkChild); }
} g_atforkRegistrar;

ShmHeader* ensureShm(Api api) {
    if (g_shm || g_failed) return g_shm;
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_shm || g_failed) return g_shm;

    std::snprintf(g_shmName, sizeof(g_shmName), "/%s%d", kShmPrefix, static_cast<int>(getpid()));
    int fd = shm_open(g_shmName, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        g_failed = true;
        return nullptr;
    }
    if (ftruncate(fd, sizeof(ShmHeader)) != 0) {
        close(fd);
        g_failed = true;
        return nullptr;
    }
    void* mem = mmap(nullptr, sizeof(ShmHeader), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mem == MAP_FAILED) {
        g_failed = true;
        return nullptr;
    }
    auto* h = static_cast<ShmHeader*>(mem);
    std::memset(static_cast<void*>(h), 0, sizeof(ShmHeader));
    h->version = kShmVersion;
    h->pid = static_cast<int32_t>(getpid());
    h->api = static_cast<uint32_t>(api);
    readProcessName(h->process_name, sizeof(h->process_name));
    h->start_ns.store(nowNs());
    // Limite inicial opcional via variável de ambiente (LINOSD_FPS_LIMIT=60).
    if (const char* lim = std::getenv("LINOSD_FPS_LIMIT")) h->fps_limit.store(static_cast<uint32_t>(std::atoi(lim)));
    std::atomic_thread_fence(std::memory_order_release);
    h->magic = kShmMagic;  // por último: indica que o cabeçalho é válido
    g_shm = h;
    std::atexit(cleanup);
    return g_shm;
}

void sleepUntil(uint64_t targetNs) {
    timespec ts;
    ts.tv_sec = static_cast<time_t>(targetNs / 1000000000ull);
    ts.tv_nsec = static_cast<long>(targetNs % 1000000000ull);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
    }
}

}  // namespace

bool hookDisabled() {
    static int disabled = -1;
    if (disabled < 0) disabled = std::getenv("LINOSD_DISABLE") ? 1 : 0;
    return disabled == 1;
}

void onPresent(Api api) {
    if (hookDisabled()) return;
    ShmHeader* h = ensureShm(api);
    if (!h) return;

    uint64_t now = nowNs();

    // Evita contar duas vezes quando o processo usa duas APIs (ex.: GL sobre Zink).
    uint64_t last = h->last_present_ns.load(std::memory_order_relaxed);
    if (h->api != static_cast<uint32_t>(api)) {
        if (last != 0 && now - last < 1000000000ull) return;
        h->api = static_cast<uint32_t>(api);
    }

    // Limitador de FPS: espera até completar o intervalo-alvo desde o último present.
    // O agendamento segue a "grade" ideal (g_lastPresent = alvo) para que atrasos do
    // despertar do kernel não se acumulem e a média fique exatamente no limite.
    uint32_t limit = h->fps_limit.load(std::memory_order_relaxed);
    if (limit > 0 && limit <= 1000 && g_lastPresent != 0) {
        uint64_t interval = 1000000000ull / limit;
        uint64_t target = g_lastPresent + interval;
        if (target > now) {
            sleepUntil(target);
            now = nowNs();
            g_lastPresent = target;
        } else {
            g_lastPresent = (now - target < interval) ? target : now;  // pequeno atraso: mantém a grade
        }
    } else {
        g_lastPresent = now;
    }

    uint64_t idx = h->frame_count.load(std::memory_order_relaxed);
    h->frame_ns[idx % kRingSize].store(now, std::memory_order_relaxed);
    h->last_present_ns.store(now, std::memory_order_relaxed);
    h->frame_count.store(idx + 1, std::memory_order_release);
}

void setResolution(uint32_t width, uint32_t height) {
    if (hookDisabled()) return;
    ShmHeader* h = ensureShm(Api::Vulkan);
    if (!h) return;
    h->width.store(width, std::memory_order_relaxed);
    h->height.store(height, std::memory_order_relaxed);
}

}  // namespace linosd
