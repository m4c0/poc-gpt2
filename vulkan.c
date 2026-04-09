#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

#define VOLK_IMPLEMENTATION
#include "volk.h"

#include "Vulkan-Headers/include/vulkan/vulkan_core.h"

#define _(X) assert(VK_SUCCESS == (X));

VkPhysicalDevice vlk_pd;
unsigned vlk_qf;
VkPipelineLayout vlk_pls[1];
VkPipeline vlk_ppls[1];

static inline VkDevice vlk_dev() { return volkGetLoadedDevice(); }

static void vlk_create_instance() {
  const char * ext[] = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
  };
  VkInstanceCreateInfo info = (VkInstanceCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
    .enabledExtensionCount = 1,
    .ppEnabledExtensionNames = ext,
  };
  VkInstance res;
  _(vkCreateInstance(&info, NULL, &res));
  volkLoadInstance(res);
}

static void vlk_find_physical_device() {
  VkPhysicalDevice pd[16];
  uint32_t pdsz = 16;
  _(vkEnumeratePhysicalDevices(volkGetLoadedInstance(), &pdsz, pd));
  for (int i = 0; i < pdsz; i++) {
    VkQueueFamilyProperties qp[16];
    uint32_t qpsz = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(pd[i], &qpsz, qp);
    for (vlk_qf = 0; vlk_qf < qpsz; vlk_qf++) {
      if ((qp[vlk_qf].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) continue;
      if ((qp[vlk_qf].queueFlags & VK_QUEUE_TRANSFER_BIT) == 0) continue;
      vlk_pd = pd[i];
      return;
    }
  }
  assert(0);
}

static void vlk_create_device() {
  const float pri = 1.0f;
  VkDeviceQueueCreateInfo q = (VkDeviceQueueCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueCount = 1,
    .pQueuePriorities = &pri,
  };
  VkDeviceCreateInfo info = (VkDeviceCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &q,
  };
  VkDevice res;
  _(vkCreateDevice(vlk_pd, &info, NULL, &res));
  volkLoadDevice(res);
}

static VkShaderModule vlk_create_shader_module() {
  FILE * f = fopen("vulkan.comp.spv", "rb");
  assert(f);
  assert(0 == fseek(f, 0, SEEK_END));
  long sz = ftell(f);
  assert(sz && (sz % 4 == 0));
  assert(0 == fseek(f, 0, SEEK_SET));
  uint32_t * data = malloc(sz);
  assert(1 == fread(data, sz, 1, f));
  fclose(f);

  VkShaderModuleCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = sz,
    .pCode = data,
  };

  VkShaderModule mod;
  _(vkCreateShaderModule(vlk_dev(), &info, NULL, &mod));

  free(data);
  return mod;
}

static void vlk_create_pipeline_layouts() {
  VkPipelineLayoutCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  };
  _(vkCreatePipelineLayout(vlk_dev(), &info, NULL, vlk_pls));
}
static void vlk_create_pipelines() {
  VkShaderModule mod = vlk_create_shader_module();

  VkComputePipelineCreateInfo infos[] = {{
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .pName = "main",
      .module = mod,
    },
    .layout = vlk_pls[0],
  }};

  _(vkCreateComputePipelines(vlk_dev(), NULL, 1, infos, NULL, vlk_ppls));
  vkDestroyShaderModule(vlk_dev(), mod, NULL);
}

int main() {
  _(volkInitialize());

  vlk_create_instance();
  vlk_find_physical_device();
  vlk_create_device();
  vlk_create_pipeline_layouts();
  vlk_create_pipelines();

  for (int i = 0; i < 1; i++) vkDestroyPipelineLayout(vlk_dev(), vlk_pls[i], NULL);
  for (int i = 0; i < 1; i++) vkDestroyPipeline(vlk_dev(), vlk_ppls[i], NULL);
  vkDestroyDevice(vlk_dev(), NULL);
  vkDestroyInstance(volkGetLoadedInstance(), NULL);
}
