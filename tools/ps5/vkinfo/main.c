/* ps5-vkinfo: M4 spike — RADV-for-PS5 feature report for DXVK-native.
 *
 * Static-ICD usage, as PS5_Vulkan's radv_smoke does: the Mesa-for-PS5 archive
 * ships no Vulkan entry points; everything is fetched through
 * vk_icdGetInstanceProcAddr (the loader-less ICD interface) and called by
 * pointer. DXVK-native cannot work that way, so this spike's second job is to
 * answer whether the entry points must be generated on top (see M4 plan).
 *
 * Enumerates: instance/device extensions, core 1.1-1.3 features, the EXT
 * feature bits DXVK 1.9.2b depends on, queues, memory heaps/types, format
 * support for the D3D11-critical formats, and the VK_KHR_display devices the
 * VideoOut swapchain exposes. Report goes to /data/ttt2-vkinfo.txt (FTP) and
 * one notification bubble announces completion.
 */

#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

/* ICD interface (defined by the RADV archive; declared by vk_icd.h). */
extern PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance, const char *);

static void Notify(const char *msg)
{
  notify_request_t req;
  memset(&req, 0, sizeof req);
  strncpy(req.message, msg, sizeof req.message - 1);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

#define GPA(v, inst, name) v.name = (typeof(v.name))icd_gpa((inst), #name)

static void DumpFeature(FILE *out, const char *name, VkBool32 v)
{
  fprintf(out, "%s=%u\n", name, (unsigned)v);
}

#define FEAT(f) DumpFeature(out, #f, f13.f)
#define FEAT12(f) DumpFeature(out, #f, f12.f)
#define FEAT11(f) DumpFeature(out, #f, f11.f)
#define FEATEXT(s, f) DumpFeature(out, #s "." #f, s.f)

int main(void)
{
  FILE *out = fopen("/data/ttt2-vkinfo.txt", "w");
  if (!out) out = fopen("/user/ttt2-vkinfo.txt", "w");
  if (!out) out = fopen("ttt2-vkinfo.txt", "w");
  if (!out) return 2;

  fprintf(out, "=== ttt2-vkinfo: RADV-for-PS5 spike (static ICD) ===\n");

  PFN_vkGetInstanceProcAddr icd_gpa =
      (PFN_vkGetInstanceProcAddr)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetInstanceProcAddr");
  if (!icd_gpa)
  {
    fprintf(out, "FATAL vk_icdGetInstanceProcAddr has no vkGetInstanceProcAddr\n");
    fclose(out);
    Notify("vkinfo FAILED (icd gpa)");
    return 1;
  }
  PFN_vkEnumerateInstanceExtensionProperties pEnumInstExt =
      (PFN_vkEnumerateInstanceExtensionProperties)icd_gpa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
  PFN_vkCreateInstance pCreateInstance =
      (PFN_vkCreateInstance)icd_gpa(VK_NULL_HANDLE, "vkCreateInstance");
  if (!pEnumInstExt || !pCreateInstance)
  {
    fprintf(out, "FATAL global procs missing (enum=%p create=%p)\n", (void *)pEnumInstExt, (void *)pCreateInstance);
    fclose(out);
    Notify("vkinfo FAILED (global procs)");
    return 1;
  }

  /* 1. instance extensions */
  uint32_t n = 0;
  VkResult r = pEnumInstExt(NULL, &n, NULL);
  fprintf(out, "instance extensions: result=%d count=%u\n", (int)r, n);
  static VkExtensionProperties e[128];
  if (r == VK_SUCCESS && n)
  {
    if (n > 128) n = 128;
    pEnumInstExt(NULL, &n, e);
    for (uint32_t i = 0; i < n; ++i)
      fprintf(out, "  %s v%u\n", e[i].extensionName, e[i].specVersion);
  }

  /* 2. instance (VK_KHR_display + surface so the VideoOut swapchain path is live) */
  VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "ttt2-vkinfo", 1, NULL, 0, VK_MAKE_VERSION(1, 3, 0)};
  const char *wantExt[] = {
      VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME,
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
      VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = 4;
  ici.ppEnabledExtensionNames = wantExt;
  VkInstance instance = VK_NULL_HANDLE;
  r = pCreateInstance(&ici, NULL, &instance);
  if (r != VK_SUCCESS)
  {
    fprintf(out, "FATAL vkCreateInstance -> %d\n", (int)r);
    fclose(out);
    Notify("vkinfo FAILED (instance)");
    return 1;
  }
  fprintf(out, "instance created: %d\n", (int)r);

  /* instance-level GPA for everything else */
#define GET(name) PFN_##name name = (PFN_##name)icd_gpa(instance, #name)
  GET(vkEnumeratePhysicalDevices);
  GET(vkGetPhysicalDeviceProperties2);
  GET(vkGetPhysicalDeviceFeatures2);
  GET(vkEnumerateDeviceExtensionProperties);
  GET(vkGetPhysicalDeviceQueueFamilyProperties2);
  GET(vkGetPhysicalDeviceMemoryProperties2);
  GET(vkGetPhysicalDeviceFormatProperties2);
  GET(vkGetPhysicalDeviceDisplayPropertiesKHR);

  /* 3. devices */
  uint32_t dn = 0;
  r = vkEnumeratePhysicalDevices(instance, &dn, NULL);
  fprintf(out, "physical devices: result=%d count=%u\n", (int)r, dn);
  if (r != VK_SUCCESS || !dn)
  {
    fclose(out);
    Notify("vkinfo: 0 devices");
    return 1;
  }
  static VkPhysicalDevice devs[8];
  if (dn > 8) dn = 8;
  vkEnumeratePhysicalDevices(instance, &dn, devs);
  VkPhysicalDevice dev = devs[0];

  VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  VkPhysicalDeviceVulkan13Properties p13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES};
  VkPhysicalDeviceDriverProperties drv = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  props.pNext = &p13;
  p13.pNext = &drv;
  vkGetPhysicalDeviceProperties2(dev, &props);
  fprintf(out, "api=%u.%u.%u driver=\"%s\" info=\"%s\" driverID=%d conformance=%u.%u.%u\n",
          VK_API_VERSION_MAJOR(props.properties.apiVersion), VK_API_VERSION_MINOR(props.properties.apiVersion),
          VK_API_VERSION_PATCH(props.properties.apiVersion), drv.driverName, drv.driverInfo, (int)drv.driverID,
          drv.conformanceVersion.major, drv.conformanceVersion.minor, drv.conformanceVersion.patch);
  fprintf(out, "limits maxPushConstants=%u maxImageDim2D=%u maxTexelBufferElements=%u maxPerStageResources=%u timestampPeriod=%f\n",
          props.properties.limits.maxPushConstantsSize, props.properties.limits.maxImageDimension2D,
          props.properties.limits.maxTexelBufferElements, props.properties.limits.maxPerStageResources,
          props.properties.limits.timestampPeriod);

  /* 4. device extensions */
  n = 0;
  vkEnumerateDeviceExtensionProperties(dev, NULL, &n, NULL);
  fprintf(out, "device extensions: %u\n", n);
  static VkExtensionProperties de[256];
  if (n > 256) n = 256;
  vkEnumerateDeviceExtensionProperties(dev, NULL, &n, de);
  for (uint32_t i = 0; i < n; ++i)
    fprintf(out, "  %s v%u\n", de[i].extensionName, de[i].specVersion);

  /* 5. features */
  VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  VkPhysicalDeviceVulkan11Features f11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
  VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceDepthClipEnableFeaturesEXT fdce = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT};
  VkPhysicalDeviceCustomBorderColorFeaturesEXT fcbc = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT};
  VkPhysicalDeviceRobustness2FeaturesEXT fr2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT feds = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
  VkPhysicalDevice4444FormatsFeaturesEXT f4444 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT};
  VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT fdem = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT};
  VkPhysicalDeviceTransformFeedbackFeaturesEXT ftf = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT};
  VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT ftba = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT};
  VkPhysicalDeviceMultiviewFeatures fmv = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
  VkPhysicalDevicePushDescriptorPropertiesKHR fpush = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR};
  f2.pNext = &f11;
  f11.pNext = &f12;
  f12.pNext = &f13;
  f13.pNext = &fdce;
  fdce.pNext = &fcbc;
  fcbc.pNext = &fr2;
  fr2.pNext = &feds;
  feds.pNext = &f4444;
  f4444.pNext = &fdem;
  fdem.pNext = &ftf;
  ftf.pNext = &ftba;
  ftba.pNext = &fmv;
  fmv.pNext = &fpush;
  vkGetPhysicalDeviceFeatures2(dev, &f2);

  fprintf(out, "--- core1.0 ---\n");
  {
    const VkPhysicalDeviceFeatures *f = &f2.features;
    fprintf(out, "geometryShader=%u tessellationShader=%u logicOp=%u dualSrcBlend=%u\n",
            f->geometryShader, f->tessellationShader, f->logicOp, f->dualSrcBlend);
    fprintf(out, "multiDrawIndirect=%u drawIndirectFirstInstance=%u depthClamp=%u depthBiasClamp=%u\n",
            f->multiDrawIndirect, f->drawIndirectFirstInstance, f->depthClamp, f->depthBiasClamp);
    fprintf(out, "fillModeNonSolid=%u depthBounds=%u wideLines=%u largePoints=%u alphaToOne=%u\n",
            f->fillModeNonSolid, f->depthBounds, f->wideLines, f->largePoints, f->alphaToOne);
    fprintf(out, "multiViewport=%u samplerAnisotropy=%u textureCompressionBC=%u occlusionQueryPrecise=%u\n",
            f->multiViewport, f->samplerAnisotropy, f->textureCompressionBC, f->occlusionQueryPrecise);
    fprintf(out, "pipelineStatisticsQuery=%u shaderClipDistance=%u shaderCullDistance=%u shaderFloat64=%u shaderInt64=%u\n",
            f->pipelineStatisticsQuery, f->shaderClipDistance, f->shaderCullDistance, f->shaderFloat64, f->shaderInt64);
    fprintf(out, "fragmentStoresAndAtomics=%u vertexPipelineStoresAndAtomics=%u shaderStorageImageWriteWithoutFormat=%u\n",
            f->fragmentStoresAndAtomics, f->vertexPipelineStoresAndAtomics, f->shaderStorageImageWriteWithoutFormat);
  }
  fprintf(out, "--- core1.1 ---\n");
  FEAT11(storageBuffer16BitAccess); FEAT11(multiview); FEAT11(variablePointers);
  FEAT11(variablePointersStorageBuffer); FEAT11(shaderDrawParameters);
  fprintf(out, "--- core1.2 ---\n");
  FEAT12(samplerMirrorClampToEdge); FEAT12(timelineSemaphore);
  FEAT12(shaderInputAttachmentArrayDynamicIndexing); FEAT12(uniformAndStorageBuffer8BitAccess);
  FEAT12(shaderSubgroupExtendedTypes); FEAT12(scalarBlockLayout);
  FEAT12(descriptorIndexing); FEAT12(shaderUniformBufferArrayNonUniformIndexing);
  FEAT12(runtimeDescriptorArray); FEAT12(descriptorBindingSampledImageUpdateAfterBind);
  FEAT12(descriptorBindingUpdateUnusedWhilePending); FEAT12(descriptorBindingPartiallyBound);
  FEAT12(descriptorBindingVariableDescriptorCount); FEAT12(shaderUniformTexelBufferArrayNonUniformIndexing);
  FEAT12(vulkanMemoryModel); FEAT12(vulkanMemoryModelDeviceScope); FEAT12(bufferDeviceAddress);
  FEAT12(hostQueryReset); FEAT12(samplerFilterMinmax);
  fprintf(out, "--- core1.3 ---\n");
  FEAT(pipelineCreationCacheControl); FEAT(privateData); FEAT(dynamicRendering);
  FEAT(shaderDemoteToHelperInvocation); FEAT(shaderTerminateInvocation);
  FEAT(synchronization2); FEAT(maintenance4); FEAT(inlineUniformBlock);
  FEAT(shaderIntegerDotProduct); FEAT(robustImageAccess);
  fprintf(out, "--- DXVK extensions ---\n");
  FEATEXT(fdce, depthClipEnable);
  FEATEXT(fcbc, customBorderColors); FEATEXT(fcbc, customBorderColorWithoutFormat);
  FEATEXT(fr2, robustBufferAccess2); FEATEXT(fr2, robustImageAccess2); FEATEXT(fr2, nullDescriptor);
  FEATEXT(feds, extendedDynamicState);
  FEATEXT(f4444, formatA4R4G4B4); FEATEXT(f4444, formatA4B4G4R4);
  FEATEXT(fdem, shaderDemoteToHelperInvocation);
  FEATEXT(ftf, transformFeedback); FEATEXT(ftf, geometryStreams);
  FEATEXT(ftba, texelBufferAlignment);
  FEATEXT(fmv, multiview); FEATEXT(fmv, multiviewGeometryShader);
  fprintf(out, "pushDescriptorMax=%u\n", fpush.maxPushDescriptors);

  /* 6. queues + memory */
  uint32_t qn = 0;
  vkGetPhysicalDeviceQueueFamilyProperties2(dev, &qn, NULL);
  fprintf(out, "queue families: %u\n", qn);
  static VkQueueFamilyProperties2 q2[8];
  static VkQueueFamilyProperties q1[8];
  if (qn > 8) qn = 8;
  for (uint32_t i = 0; i < qn; ++i) { q2[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2; q2[i].queueFamilyProperties = q1[i]; }
  vkGetPhysicalDeviceQueueFamilyProperties2(dev, &qn, q2);
  for (uint32_t i = 0; i < qn; ++i)
    fprintf(out, "  q%u: flags=%x count=%u ts=%u gran=%ux%ux%u\n", i,
            q2[i].queueFamilyProperties.queueFlags, q2[i].queueFamilyProperties.queueCount,
            q2[i].queueFamilyProperties.timestampValidBits,
            q2[i].queueFamilyProperties.minImageTransferGranularity.width,
            q2[i].queueFamilyProperties.minImageTransferGranularity.height,
            q2[i].queueFamilyProperties.minImageTransferGranularity.depth);

  VkPhysicalDeviceMemoryProperties2 mem = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
  vkGetPhysicalDeviceMemoryProperties2(dev, &mem);
  fprintf(out, "memory: types=%u heaps=%u\n", mem.memoryProperties.memoryTypeCount, mem.memoryProperties.memoryHeapCount);
  for (uint32_t i = 0; i < mem.memoryProperties.memoryTypeCount; ++i)
    fprintf(out, "  type%u heap=%u flags=%x\n", i, mem.memoryProperties.memoryTypes[i].heapIndex, mem.memoryProperties.memoryTypes[i].propertyFlags);
  for (uint32_t i = 0; i < mem.memoryProperties.memoryHeapCount; ++i)
    fprintf(out, "  heap%u size=%lluMB flags=%x\n", i, (unsigned long long)(mem.memoryProperties.memoryHeaps[i].size >> 20), mem.memoryProperties.memoryHeaps[i].flags);

  /* 7. displays (the VideoOut swapchain path) */
  if (vkGetPhysicalDeviceDisplayPropertiesKHR)
  {
    uint32_t dispn = 0;
    r = vkGetPhysicalDeviceDisplayPropertiesKHR(dev, &dispn, NULL);
    fprintf(out, "displays (VK_KHR_display): result=%d count=%u\n", (int)r, dispn);
    if (r == VK_SUCCESS && dispn)
    {
      static VkDisplayPropertiesKHR dp[8];
      uint32_t dn2 = dispn < 8 ? dispn : 8;
      vkGetPhysicalDeviceDisplayPropertiesKHR(dev, &dn2, dp);
      for (uint32_t i = 0; i < dn2; ++i)
        fprintf(out, "  display%u: \"%s\" res=%ux%u physical_mm=%ux%u\n", i, dp[i].displayName,
                dp[i].physicalResolution.width, dp[i].physicalResolution.height,
                dp[i].physicalDimensions.width, dp[i].physicalDimensions.height);
    }
  }
  else
    fprintf(out, "displays (VK_KHR_display): procaddr NULL\n");

  /* 8. D3D11-critical format support */
  static const VkFormat fmts[] = {
      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM,
      VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R16G16B16A16_UNORM,
      VK_FORMAT_R8_UNORM, VK_FORMAT_R8_SNORM, VK_FORMAT_R16_SFLOAT, VK_FORMAT_R32_SFLOAT,
      VK_FORMAT_R32_SINT, VK_FORMAT_R32_UINT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_B10G11R11_UFLOAT_PACK32,
      VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
      VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK,
      VK_FORMAT_BC4_UNORM_BLOCK, VK_FORMAT_BC5_SNORM_BLOCK, VK_FORMAT_BC6H_SFLOAT_BLOCK,
      VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_D16_UNORM, VK_FORMAT_D24_UNORM_S8_UINT,
      VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_B5G6R5_UNORM_PACK16,
      VK_FORMAT_B4G4R4A4_UNORM_PACK16, VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT};
  fprintf(out, "--- format support ---\n");
  for (uint32_t i = 0; i < sizeof fmts / sizeof fmts[0]; ++i)
  {
    VkFormatProperties2 fp = {VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
    vkGetPhysicalDeviceFormatProperties2(dev, fmts[i], &fp);
    fprintf(out, "  fmt %u: linear=%x optimal=%x buffer=%x\n", (unsigned)fmts[i],
            fp.formatProperties.linearTilingFeatures, fp.formatProperties.optimalTilingFeatures,
            fp.formatProperties.bufferFeatures);
  }

  fprintf(out, "=== END vkinfo ===\n");
  fclose(out);
  Notify("vkinfo OK: /data/ttt2-vkinfo.txt");
  return 0;
}
