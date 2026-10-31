// LinOSD - protocolo de memória compartilhada entre o hook (dentro do jogo)
// e o aplicativo de overlay.
//
// Cada processo que apresenta frames cria /dev/shm/linosd.<pid>. O layout é
// idêntico em processos 32 e 64 bits (todos os campos de 64 bits têm
// alignas(8)), permitindo que um jogo 32-bit seja lido pelo overlay 64-bit.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace linosd {

constexpr uint32_t kShmMagic = 0x4C4F5344;  // "LOSD"
constexpr uint32_t kShmVersion = 2;
constexpr uint32_t kRingSize = 16384;       // ~16 s a 1000 FPS
constexpr const char* kShmPrefix = "linosd.";

enum class Api : uint32_t {
    Unknown = 0,
    Vulkan = 1,
    OpenGL = 2,  // GLX
    EGL = 3,
};

inline const char* apiName(uint32_t api) {
    switch (static_cast<Api>(api)) {
        case Api::Vulkan: return "Vulkan";
        case Api::OpenGL: return "OpenGL";
        case Api::EGL: return "OpenGL (EGL)";
        default: return "?";
    }
}

struct ShmHeader {
    uint32_t magic;
    uint32_t version;
    int32_t pid;
    uint32_t api;                      // Api atual (o primeiro que apresentou)
    char process_name[64];

    alignas(8) std::atomic<uint64_t> frame_count;     // total de frames gravados
    alignas(8) std::atomic<uint64_t> last_present_ns; // CLOCK_MONOTONIC
    alignas(8) std::atomic<uint64_t> start_ns;

    // Escrito pelo overlay, lido pelo hook.
    alignas(8) std::atomic<uint32_t> fps_limit;       // 0 = desligado
    std::atomic<uint32_t> reserved0;

    // Informação do swapchain (somente Vulkan).
    alignas(8) std::atomic<uint32_t> width;
    std::atomic<uint32_t> height;

    alignas(8) uint64_t reserved[8];

    // Timestamp (ns, CLOCK_MONOTONIC) de cada present; índice = frame % kRingSize.
    alignas(8) std::atomic<uint64_t> frame_ns[kRingSize];
};

static_assert(sizeof(std::atomic<uint64_t>) == 8, "atomic<uint64_t> deve ter 8 bytes");
static_assert(offsetof(ShmHeader, frame_count) == 80, "layout incompatível entre 32/64 bits");
static_assert(sizeof(ShmHeader) % 8 == 0, "tamanho deve ser múltiplo de 8");

}  // namespace linosd
