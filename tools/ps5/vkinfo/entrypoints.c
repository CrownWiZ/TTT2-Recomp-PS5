/* vkinfo entry-point trampolines.
 *
 * Mesa's vk_entrypoints_gen --weak ships the radv_* entry points as weak
 * references with no definition: the dispatch tables and
 * vk_icdGetInstanceProcAddr resolve them only if real definitions exist at
 * static link time (PS5_Vulkan's titles get them from the ps5vk driver
 * archive). This spike links no driver archive, so it provides strong
 * definitions for the subset it needs: trampolines into Mesa's runtime
 * (vk_common_*) and the PS5 VideoOut swapchain's wsi_* implementations.
 */
#include <vulkan/vulkan.h>
#include <string.h>

#define TRAMP(ret, fqn, wsi, sig_params, call_args) \
    extern VKAPI_ATTR ret VKAPI_CALL wsi sig_params; \
    VKAPI_ATTR ret VKAPI_CALL fqn sig_params { return wsi call_args; }

/* --- instance/global level --- */
extern VKAPI_ATTR VkResult VKAPI_CALL vk_common_EnumeratePhysicalDevices(VkInstance instance, uint32_t *count, VkPhysicalDevice *devs);
VKAPI_ATTR VkResult VKAPI_CALL radv_EnumeratePhysicalDevices(VkInstance instance, uint32_t *count, VkPhysicalDevice *devs)
{
    return vk_common_EnumeratePhysicalDevices(instance, count, devs);
}

extern VKAPI_ATTR void VKAPI_CALL vk_common_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice dev, VkPhysicalDeviceMemoryProperties *props);
VKAPI_ATTR void VKAPI_CALL radv_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice dev, VkPhysicalDeviceMemoryProperties *props)
{
    vk_common_GetPhysicalDeviceMemoryProperties(dev, props);
}

/* --- VK_KHR_surface / VK_KHR_display: the PS5 VideoOut swapchain --- */
extern VKAPI_ATTR VkResult VKAPI_CALL wsi_CreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
VKAPI_ATTR VkResult VKAPI_CALL radv_CreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
    return wsi_CreateDisplayPlaneSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_CreateHeadlessSurfaceEXT(VkInstance instance, const VkHeadlessSurfaceCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
VKAPI_ATTR VkResult VKAPI_CALL radv_CreateHeadlessSurfaceEXT(VkInstance instance, const VkHeadlessSurfaceCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
    return wsi_CreateHeadlessSurfaceEXT(instance, pCreateInfo, pAllocator, pSurface);
}

extern VKAPI_ATTR void VKAPI_CALL wsi_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR void VKAPI_CALL radv_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator)
{
    wsi_DestroySurfaceKHR(instance, surface, pAllocator);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice dev, uint32_t *count, VkDisplayPropertiesKHR *props);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice dev, uint32_t *count, VkDisplayPropertiesKHR *props)
{
    return wsi_GetPhysicalDeviceDisplayPropertiesKHR(dev, count, props);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDeviceDisplayProperties2KHR(VkPhysicalDevice dev, uint32_t *count, VkDisplayProperties2KHR *props);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceDisplayProperties2KHR(VkPhysicalDevice dev, uint32_t *count, VkDisplayProperties2KHR *props)
{
    return wsi_GetPhysicalDeviceDisplayProperties2KHR(dev, count, props);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice dev, uint32_t *count, VkDisplayPlanePropertiesKHR *props);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice dev, uint32_t *count, VkDisplayPlanePropertiesKHR *props)
{
    return wsi_GetPhysicalDeviceDisplayPlanePropertiesKHR(dev, count, props);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDeviceDisplayPlaneProperties2KHR(VkPhysicalDevice dev, uint32_t *count, VkDisplayPlaneProperties2KHR *props);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceDisplayPlaneProperties2KHR(VkPhysicalDevice dev, uint32_t *count, VkDisplayPlaneProperties2KHR *props)
{
    return wsi_GetPhysicalDeviceDisplayPlaneProperties2KHR(dev, count, props);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice dev, uint32_t planeIndex, uint32_t *count, VkDisplayKHR *displays);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice dev, uint32_t planeIndex, uint32_t *count, VkDisplayKHR *displays)
{
    return wsi_GetDisplayPlaneSupportedDisplaysKHR(dev, planeIndex, count, displays);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetDisplayModePropertiesKHR(VkPhysicalDevice dev, VkDisplayKHR display, uint32_t *count, VkDisplayModePropertiesKHR *props);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetDisplayModePropertiesKHR(VkPhysicalDevice dev, VkDisplayKHR display, uint32_t *count, VkDisplayModePropertiesKHR *props)
{
    return wsi_GetDisplayModePropertiesKHR(dev, display, count, props);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetDisplayModeProperties2KHR(VkPhysicalDevice dev, VkDisplayKHR display, uint32_t *count, VkDisplayModeProperties2KHR *props);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetDisplayModeProperties2KHR(VkPhysicalDevice dev, VkDisplayKHR display, uint32_t *count, VkDisplayModeProperties2KHR *props)
{
    return wsi_GetDisplayModeProperties2KHR(dev, display, count, props);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_CreateDisplayModeKHR(VkPhysicalDevice dev, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDisplayModeKHR *pMode);
VKAPI_ATTR VkResult VKAPI_CALL radv_CreateDisplayModeKHR(VkPhysicalDevice dev, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDisplayModeKHR *pMode)
{
    return wsi_CreateDisplayModeKHR(dev, display, pCreateInfo, pAllocator, pMode);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice dev, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR *caps);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice dev, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR *caps)
{
    return wsi_GetDisplayPlaneCapabilitiesKHR(dev, mode, planeIndex, caps);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetDisplayPlaneCapabilities2KHR(VkPhysicalDevice dev, const VkDisplayPlaneInfo2KHR *pPlaneInfo, VkDisplayPlaneCapabilities2KHR *caps);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetDisplayPlaneCapabilities2KHR(VkPhysicalDevice dev, const VkDisplayPlaneInfo2KHR *pPlaneInfo, VkDisplayPlaneCapabilities2KHR *caps)
{
    return wsi_GetDisplayPlaneCapabilities2KHR(dev, pPlaneInfo, caps);
}

/* --- swapchain (device side) --- */
extern VKAPI_ATTR VkResult VKAPI_CALL wsi_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain);
VKAPI_ATTR VkResult VKAPI_CALL radv_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain)
{
    return wsi_CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
}

extern VKAPI_ATTR void VKAPI_CALL wsi_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR void VKAPI_CALL radv_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator)
{
    wsi_DestroySwapchainKHR(device, swapchain, pAllocator);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *count, VkImage *images);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *count, VkImage *images)
{
    return wsi_GetSwapchainImagesKHR(device, swapchain, count, images);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *index);
VKAPI_ATTR VkResult VKAPI_CALL radv_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *index)
{
    return wsi_AcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, index);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo, uint32_t *pImageIndex);
VKAPI_ATTR VkResult VKAPI_CALL radv_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo, uint32_t *pImageIndex)
{
    return wsi_AcquireNextImage2KHR(device, pAcquireInfo, pImageIndex);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo);
VKAPI_ATTR VkResult VKAPI_CALL radv_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
    return wsi_QueuePresentKHR(queue, pPresentInfo);
}

/* --- surface queries (physical device) --- */
extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice dev, uint32_t queueFamily, VkSurfaceKHR surface, VkBool32 *supported);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice dev, uint32_t queueFamily, VkSurfaceKHR surface, VkBool32 *supported)
{
    return wsi_GetPhysicalDeviceSurfaceSupportKHR(dev, queueFamily, surface, supported);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice dev, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *caps);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice dev, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *caps)
{
    return wsi_GetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, caps);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice dev, const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo, VkSurfaceCapabilities2KHR *caps);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice dev, const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo, VkSurfaceCapabilities2KHR *caps)
{
    return wsi_GetPhysicalDeviceSurfaceCapabilities2KHR(dev, pSurfaceInfo, caps);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice dev, VkSurfaceKHR surface, uint32_t *count, VkSurfaceFormatKHR *formats);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice dev, VkSurfaceKHR surface, uint32_t *count, VkSurfaceFormatKHR *formats)
{
    return wsi_GetPhysicalDeviceSurfaceFormatsKHR(dev, surface, count, formats);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice dev, const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo, uint32_t *count, VkSurfaceFormat2KHR *formats);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice dev, const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo, uint32_t *count, VkSurfaceFormat2KHR *formats)
{
    return wsi_GetPhysicalDeviceSurfaceFormats2KHR(dev, pSurfaceInfo, count, formats);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice dev, VkSurfaceKHR surface, uint32_t *count, VkPresentModeKHR *modes);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice dev, VkSurfaceKHR surface, uint32_t *count, VkPresentModeKHR *modes)
{
    return wsi_GetPhysicalDeviceSurfacePresentModesKHR(dev, surface, count, modes);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice dev, VkSurfaceKHR surface, uint32_t *count, VkRect2D *rects);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice dev, VkSurfaceKHR surface, uint32_t *count, VkRect2D *rects)
{
    return wsi_GetPhysicalDevicePresentRectanglesKHR(dev, surface, count, rects);
}

/* --- presentation timing (GOOGLE/EXT) --- */
extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetRefreshCycleDurationGOOGLE(VkDevice device, VkSwapchainKHR swapchain, VkRefreshCycleDurationGOOGLE *out);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetRefreshCycleDurationGOOGLE(VkDevice device, VkSwapchainKHR swapchain, VkRefreshCycleDurationGOOGLE *out)
{
    return wsi_GetRefreshCycleDurationGOOGLE(device, swapchain, out);
}

extern VKAPI_ATTR VkResult VKAPI_CALL wsi_GetPastPresentationTimingGOOGLE(VkDevice device, VkSwapchainKHR swapchain, uint32_t *count, VkPastPresentationTimingGOOGLE *timings);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPastPresentationTimingGOOGLE(VkDevice device, VkSwapchainKHR swapchain, uint32_t *count, VkPastPresentationTimingGOOGLE *timings)
{
    return wsi_GetPastPresentationTimingGOOGLE(device, swapchain, count, timings);
}

extern VKAPI_ATTR void VKAPI_CALL wsi_SetHdrMetadataEXT(VkDevice device, uint32_t count, const VkSwapchainKHR *swapchains, const VkHdrMetadataEXT *metadata);
VKAPI_ATTR void VKAPI_CALL radv_SetHdrMetadataEXT(VkDevice device, uint32_t count, const VkSwapchainKHR *swapchains, const VkHdrMetadataEXT *metadata)
{
    wsi_SetHdrMetadataEXT(device, count, swapchains, metadata);
}

/* --- debug callbacks: wired unconditionally; trampolines into the runtime --- */
extern VKAPI_ATTR VkResult VKAPI_CALL vk_common_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugReportCallbackEXT *pCallback);
VKAPI_ATTR VkResult VKAPI_CALL radv_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugReportCallbackEXT *pCallback)
{
    return vk_common_CreateDebugReportCallbackEXT(instance, pCreateInfo, pAllocator, pCallback);
}

extern VKAPI_ATTR void VKAPI_CALL vk_common_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR void VKAPI_CALL radv_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks *pAllocator)
{
    vk_common_DestroyDebugReportCallbackEXT(instance, callback, pAllocator);
}

extern VKAPI_ATTR void VKAPI_CALL vk_common_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char *pLayerPrefix, const char *pMessage);
VKAPI_ATTR void VKAPI_CALL radv_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char *pLayerPrefix, const char *pMessage)
{
    vk_common_DebugReportMessageEXT(instance, flags, objectType, object, location, messageCode, pLayerPrefix, pMessage);
}

extern VKAPI_ATTR VkResult VKAPI_CALL vk_common_CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *messenger);
VKAPI_ATTR VkResult VKAPI_CALL radv_CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *messenger)
{
    return vk_common_CreateDebugUtilsMessengerEXT(instance, pCreateInfo, pAllocator, messenger);
}

extern VKAPI_ATTR void VKAPI_CALL vk_common_DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR void VKAPI_CALL radv_DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks *pAllocator)
{
    vk_common_DestroyDebugUtilsMessengerEXT(instance, messenger, pAllocator);
}

extern VKAPI_ATTR void VKAPI_CALL vk_common_SubmitDebugUtilsMessageEXT(VkInstance instance, VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData);
VKAPI_ATTR void VKAPI_CALL radv_SubmitDebugUtilsMessageEXT(VkInstance instance, VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData)
{
    vk_common_SubmitDebugUtilsMessageEXT(instance, messageSeverity, messageTypes, pCallbackData);
}

/* --- device groups (wired unconditionally; runtime implementations) --- */
extern VKAPI_ATTR VkResult VKAPI_CALL vk_common_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *count, VkPhysicalDeviceGroupProperties *groups);
VKAPI_ATTR VkResult VKAPI_CALL radv_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *count, VkPhysicalDeviceGroupProperties *groups)
{
    return vk_common_EnumeratePhysicalDeviceGroups(instance, count, groups);
}

VKAPI_ATTR VkResult VKAPI_CALL radv_EnumeratePhysicalDeviceGroupsKHR(VkInstance instance, uint32_t *count, VkPhysicalDeviceGroupProperties *groups)
{
    return vk_common_EnumeratePhysicalDeviceGroups(instance, count, groups);
}

extern VKAPI_ATTR VkResult VKAPI_CALL vk_common_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice dev, const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo, VkExternalSemaphoreProperties *pExternalSemaphoreProperties);
VKAPI_ATTR VkResult VKAPI_CALL radv_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice dev, const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo, VkExternalSemaphoreProperties *pExternalSemaphoreProperties)
{
    return vk_common_GetPhysicalDeviceExternalSemaphoreProperties(dev, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
}

/* Mesa's own fallback for entry points the driver does not provide: the
 * generated tables name every entry point weakly and the und ones would
 * resolve to NULL. The native-tool refuses undefined symbols, so the mass
 * --defsym pass in the build script binds every remaining weak entry point
 * here. */
VKAPI_ATTR VkResult VKAPI_CALL vk_entrypoint_stub(void)
{
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

