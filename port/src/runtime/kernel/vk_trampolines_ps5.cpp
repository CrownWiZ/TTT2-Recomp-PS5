// Vulkan trampolines for the static RADV.
//
// DXVK resolves Vulkan through its own loader, which calls libc-level
// symbols (vkCreateInstance, vkGetInstanceProcAddr). A static ICD does not
// export those names; the stack's static-ICD pattern (the M4 ledger ruling)
// resolves every global by name through the ICD entry point:
// vk_icdGetInstanceProcAddr(NULL, "<name>"). Each trampoline resolves its
// name once (safe static initialization) and flows by pointers after that.
#include <vulkan/vulkan.h>

extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance, const char*);

extern "C" VkResult vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    static auto fn = vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    return fn ? reinterpret_cast<PFN_vkCreateInstance>(fn)(pCreateInfo, pAllocator, pInstance)
              : VK_ERROR_INITIALIZATION_FAILED;
}

extern "C" VkResult vkEnumerateInstanceExtensionProperties(
    const char* layerName, uint32_t* propertyCount, VkExtensionProperties* properties)
{
    static auto fn = vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    return fn ? reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(fn)(layerName, propertyCount, properties)
              : VK_ERROR_INITIALIZATION_FAILED;
}

extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char* name)
{
    return vk_icdGetInstanceProcAddr(instance, name);
}
