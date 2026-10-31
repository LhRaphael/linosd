// Gravação de frames dentro do processo do jogo.
#pragma once

#include <cstdint>

#include "../common/shm_protocol.h"

namespace linosd {

// Chamado a cada present (vkQueuePresentKHR / glXSwapBuffers / eglSwapBuffers).
// Aplica o limitador de FPS (se configurado pelo overlay) e grava o timestamp.
void onPresent(Api api);

// Informa a resolução do swapchain (Vulkan).
void setResolution(uint32_t width, uint32_t height);

// true se a variável LINOSD_DISABLE estiver definida.
bool hookDisabled();

}  // namespace linosd
