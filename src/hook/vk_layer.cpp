// LinOSD - camada implícita Vulkan.
//
// Intercepta vkQueuePresentKHR para medir frametimes e vkCreateSwapchainKHR
// para obter a resolução. Habilitada apenas quando LINOSD=1 (ver manifesto).
// As estruturas da interface loader<->layer são declaradas aqui (layout
// estável, definido em vk_layer.h) para não depender da versão dos headers.
#include <vulkan/vulkan.h>

#include <cstring>
#include <mutex>
#include <unordered_map>

#include "frame_recorder.h"

#define LINOSD_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

// ---------------------------------------------------------------------------
// Interface mínima do loader (equivalente a vulkan/vk_layer.h)
// ---------------------------------------------------------------------------
enum LayerFunction { LAYER_LINK_INFO = 0 };

typedef PFN_vkVoidFunction(VKAPI_PTR* PFN_GetPhysicalDeviceProcAddr)(VkInstance, const char*);

struct LayerInstanceLink {
    LayerInstanceLink* pNext;
    PFN_vkGetInstanceProcAddr pfnNextGetInstanceProcAddr;
    PFN_GetPhysicalDeviceProcAddr pfnNextGetPhysicalDeviceProcAddr;
};

struct LayerInstanceCreateInfo {
    VkStructureType sType;  // VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO
    const void* pNext;
    LayerFunction function;
    union {
        LayerInstanceLink* pLayerInfo;
        void* other[2];
    } u;
};

struct LayerDeviceLink {
    LayerDeviceLink* pNext;
    PFN_vkGetInstanceProcAddr pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr pfnNextGetDeviceProcAddr;
};

struct LayerDeviceCreateInfo {
    VkStructureType sType;  // VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
    const void* pNext;
    LayerFunction function;
    union {
        LayerDeviceLink* pLayerInfo;
        void* other;
    } u;
};

enum NegotiateLayerStructType { LAYER_NEGOTIATE_INTERFACE_STRUCT = 1 };

struct NegotiateLayerInterface {
    NegotiateLayerStructType sType;
    void* pNext;
    uint32_t loaderLayerInterfaceVersion;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr pfnGetDeviceProcAddr;
    PFN_GetPhysicalDeviceProcAddr pfnGetPhysicalDeviceProcAddr;
};

// ---------------------------------------------------------------------------
// Tabelas de despacho (chave = ponteiro de despacho do objeto)
// ---------------------------------------------------------------------------
struct InstanceData {
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
};

struct DeviceData {
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkQueuePresentKHR queuePresent = nullptr;
    PFN_vkCreateSwapchainKHR createSwapchain = nullptr;
};

std::mutex g_mutex;
std::unordered_map<void*, InstanceData> g_instances;
std::unordered_map<void*, DeviceData> g_devices;

template <typename T>
void* key(T handle) {
    return *reinterpret_cast<void**>(handle);
}

DeviceData* deviceFor(void* k) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_devices.find(k);
    return it == g_devices.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// Funções interceptadas
// ---------------------------------------------------------------------------
VKAPI_ATTR VkResult VKAPI_CALL layer_CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                                    const VkAllocationCallbacks* pAllocator,
                                                    VkInstance* pInstance) {
    auto* chain = reinterpret_cast<LayerInstanceCreateInfo*>(const_cast<void*>(pCreateInfo->pNext));
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                      chain->function == LAYER_LINK_INFO))
        chain = reinterpret_cast<LayerInstanceCreateInfo*>(const_cast<void*>(chain->pNext));
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;  // avança para a próxima camada

    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(nextGipa(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!createInstance) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult res = createInstance(pCreateInfo, pAllocator, pInstance);
    if (res != VK_SUCCESS) return res;

    InstanceData data;
    data.gipa = nextGipa;
    data.destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(nextGipa(*pInstance, "vkDestroyInstance"));
    std::lock_guard<std::mutex> lock(g_mutex);
    g_instances[key(*pInstance)] = data;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layer_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyInstance destroy = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_instances.find(key(instance));
        if (it != g_instances.end()) {
            destroy = it->second.destroyInstance;
            g_instances.erase(it);
        }
    }
    if (destroy) destroy(instance, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateDevice(VkPhysicalDevice physicalDevice,
                                                  const VkDeviceCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    auto* chain = reinterpret_cast<LayerDeviceCreateInfo*>(const_cast<void*>(pCreateInfo->pNext));
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                      chain->function == LAYER_LINK_INFO))
        chain = reinterpret_cast<LayerDeviceCreateInfo*>(const_cast<void*>(chain->pNext));
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

    auto createDevice = reinterpret_cast<PFN_vkCreateDevice>(nextGipa(VK_NULL_HANDLE, "vkCreateDevice"));
    if (!createDevice) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult res = createDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (res != VK_SUCCESS) return res;

    DeviceData data;
    data.gdpa = nextGdpa;
    data.destroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(nextGdpa(*pDevice, "vkDestroyDevice"));
    data.queuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(nextGdpa(*pDevice, "vkQueuePresentKHR"));
    data.createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(nextGdpa(*pDevice, "vkCreateSwapchainKHR"));
    std::lock_guard<std::mutex> lock(g_mutex);
    g_devices[key(*pDevice)] = data;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layer_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyDevice destroy = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_devices.find(key(device));
        if (it != g_devices.end()) {
            destroy = it->second.destroyDevice;
            g_devices.erase(it);
        }
    }
    if (destroy) destroy(device, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkSwapchainKHR* pSwapchain) {
    DeviceData* d = deviceFor(key(device));
    if (!d || !d->createSwapchain) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult res = d->createSwapchain(device, pCreateInfo, pAllocator, pSwapchain);
    if (res == VK_SUCCESS) linosd::setResolution(pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    // VkQueue compartilha o ponteiro de despacho com o VkDevice de origem.
    DeviceData* d = deviceFor(key(queue));
    if (!d || !d->queuePresent) return VK_ERROR_DEVICE_LOST;
    linosd::onPresent(linosd::Api::Vulkan);
    return d->queuePresent(queue, pPresentInfo);
}

PFN_vkVoidFunction interceptDevice(const char* name) {
    if (!std::strcmp(name, "vkGetDeviceProcAddr")) return nullptr;  // tratado abaixo
    if (!std::strcmp(name, "vkDestroyDevice")) return reinterpret_cast<PFN_vkVoidFunction>(layer_DestroyDevice);
    if (!std::strcmp(name, "vkQueuePresentKHR")) return reinterpret_cast<PFN_vkVoidFunction>(layer_QueuePresentKHR);
    if (!std::strcmp(name, "vkCreateSwapchainKHR"))
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateSwapchainKHR);
    return nullptr;
}

}  // namespace

LINOSD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL linosd_GetDeviceProcAddr(VkDevice device, const char* pName);

LINOSD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL linosd_GetInstanceProcAddr(VkInstance instance,
                                                                                 const char* pName) {
    if (!std::strcmp(pName, "vkGetInstanceProcAddr"))
        return reinterpret_cast<PFN_vkVoidFunction>(linosd_GetInstanceProcAddr);
    if (!std::strcmp(pName, "vkGetDeviceProcAddr"))
        return reinterpret_cast<PFN_vkVoidFunction>(linosd_GetDeviceProcAddr);
    if (!std::strcmp(pName, "vkCreateInstance")) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateInstance);
    if (!std::strcmp(pName, "vkDestroyInstance")) return reinterpret_cast<PFN_vkVoidFunction>(layer_DestroyInstance);
    if (!std::strcmp(pName, "vkCreateDevice")) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateDevice);
    if (PFN_vkVoidFunction f = interceptDevice(pName)) return f;

    if (instance == VK_NULL_HANDLE) return nullptr;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_instances.find(key(instance));
        if (it != g_instances.end()) gipa = it->second.gipa;
    }
    return gipa ? gipa(instance, pName) : nullptr;
}

LINOSD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL linosd_GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (!std::strcmp(pName, "vkGetDeviceProcAddr"))
        return reinterpret_cast<PFN_vkVoidFunction>(linosd_GetDeviceProcAddr);
    DeviceData* d = deviceFor(key(device));
    if (PFN_vkVoidFunction f = interceptDevice(pName)) {
        // Só intercepta se a próxima camada/driver expõe a função.
        if (d && d->gdpa && d->gdpa(device, pName)) return f;
        return nullptr;
    }
    return (d && d->gdpa) ? d->gdpa(device, pName) : nullptr;
}

LINOSD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL linosd_NegotiateLoaderLayerInterfaceVersion(void* pVersionStruct) {
    auto* s = static_cast<NegotiateLayerInterface*>(pVersionStruct);
    if (!s || s->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (s->loaderLayerInterfaceVersion >= 2) {
        s->pfnGetInstanceProcAddr = linosd_GetInstanceProcAddr;
        s->pfnGetDeviceProcAddr = linosd_GetDeviceProcAddr;
        s->pfnGetPhysicalDeviceProcAddr = nullptr;
        s->loaderLayerInterfaceVersion = 2;
    }
    return VK_SUCCESS;
}
