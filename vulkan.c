#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

#define VOLK_IMPLEMENTATION
#include "volk.h"

#include "Vulkan-Headers/include/vulkan/vulkan_core.h"

#define _(X) assert(VK_SUCCESS == (X));

static VkBuffer vlk_bufs[2];
static VkCommandBuffer vlk_cb;
static VkCommandPool vlk_cpool;
static VkDescriptorPool vlk_dpools[1];
static VkDescriptorSet vlk_dsets[1];
static VkDescriptorSetLayout vlk_dsls[1];
static VkDeviceMemory vlk_mems[2];
static VkPhysicalDevice vlk_pd;
static VkPipeline vlk_ppls[1];
static VkPipelineLayout vlk_pls[1];
static VkQueue vlk_q;
static unsigned vlk_qf;

static inline VkDevice vlk_dev() { return volkGetLoadedDevice(); }

static void vlk_create_instance() {
  const char * ext[] = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
  };
  VkApplicationInfo app = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .apiVersion = VK_API_VERSION_1_2,
  };
  VkInstanceCreateInfo info = (VkInstanceCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
    .enabledExtensionCount = 1,
    .ppEnabledExtensionNames = ext,
    .pApplicationInfo = &app,
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
    .queueFamilyIndex = vlk_qf,
  };
  VkDeviceCreateInfo info = (VkDeviceCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &q,
  };
#ifdef __APPLE__
  const char * ext[1] = { "VK_KHR_portability_subset" };
  info.ppEnabledExtensionNames = ext;
  info.enabledExtensionCount = 1;
#endif
  VkDevice res;
  _(vkCreateDevice(vlk_pd, &info, NULL, &res));
  volkLoadDevice(res);

  vkGetDeviceQueue(res, vlk_qf, 0, &vlk_q);
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

static void vlk_create_descriptor_set_layouts() {
  VkDescriptorSetLayoutBinding bis[] = {{
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  }, {
    .binding = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  }};
  VkDescriptorSetLayoutCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = 2,
    .pBindings = bis,
  };
  _(vkCreateDescriptorSetLayout(vlk_dev(), &info, NULL, vlk_dsls));
}

static void vlk_create_descriptor_pool() {
  VkDescriptorPoolSize pszs[1] = {{
    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 2,
  }};
  VkDescriptorPoolCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1,
    .poolSizeCount = 1,
    .pPoolSizes = pszs,
  };
  _(vkCreateDescriptorPool(vlk_dev(), &info, NULL, vlk_dpools));
}

static void vlk_allocate_descriptor_set() {
  VkDescriptorSetAllocateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = vlk_dpools[0],
    .descriptorSetCount = 1,
    .pSetLayouts = vlk_dsls,
  };
  _(vkAllocateDescriptorSets(vlk_dev(), &info, vlk_dsets));
}

static void vlk_create_pipeline_layouts() {
  VkPipelineLayoutCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = vlk_dsls,
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

static void vlk_create_buffers() {
  VkBufferCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = sizeof(float),
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
  };
  _(vkCreateBuffer(vlk_dev(), &info, NULL, &vlk_bufs[0]));

  info = (VkBufferCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = sizeof(float),
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
  };
  _(vkCreateBuffer(vlk_dev(), &info, NULL, &vlk_bufs[1]));
}

static void vlk_allocate_memories() {
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(vlk_pd, &props);

  int local = -1, host = -1;
  for (int i = 0; i < props.memoryTypeCount; i++) {
    VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
    if (local == -1 && (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) local = i;
    if (host == -1 && (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) host = i;
  }
  assert(local >= 0);
  assert(host >= 0);

  VkMemoryAllocateInfo info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = sizeof(float),
    .memoryTypeIndex = local,
  };
  _(vkAllocateMemory(vlk_dev(), &info, NULL, &vlk_mems[0]));

  info = (VkMemoryAllocateInfo) {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = sizeof(float),
    .memoryTypeIndex = host,
  };
  _(vkAllocateMemory(vlk_dev(), &info, NULL, &vlk_mems[1]));
}

static void vlk_create_command_pool() {
  VkCommandPoolCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
  };
  _(vkCreateCommandPool(vlk_dev(), &info, NULL, &vlk_cpool));
}

static void vlk_create_command_buffer() {
  VkCommandBufferAllocateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = vlk_cpool,
    .commandBufferCount = 1,
  };
  _(vkAllocateCommandBuffers(vlk_dev(), &info, &vlk_cb));
}

static void vlk_begin_command_buffer() {
  VkCommandBufferBeginInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  vkBeginCommandBuffer(vlk_cb, &info);
}
static void vlk_end_command_buffer() {
  vkEndCommandBuffer(vlk_cb);
}

static void vlk_submit() {
  VkSubmitInfo info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pCommandBuffers = &vlk_cb,
    .commandBufferCount = 1,
  };
  _(vkQueueSubmit(vlk_q, 1, &info, NULL));
}

int main() {
  _(volkInitialize());

  vlk_create_instance();
  vlk_find_physical_device();
  vlk_create_device();
  vlk_create_descriptor_pool();
  vlk_create_descriptor_set_layouts();
  vlk_allocate_descriptor_set();
  vlk_create_pipeline_layouts();
  vlk_create_pipelines();
  vlk_create_buffers();
  vlk_allocate_memories();
  vlk_create_command_pool();
  vlk_create_command_buffer();

  _(vkBindBufferMemory(vlk_dev(), vlk_bufs[0], vlk_mems[0], 0));
  _(vkBindBufferMemory(vlk_dev(), vlk_bufs[1], vlk_mems[1], 0));

  const float k = 67;

  vlk_begin_command_buffer();
  vkCmdUpdateBuffer(vlk_cb, vlk_bufs[0], 0, sizeof(float), &k);
  vkCmdBindPipeline(vlk_cb, VK_PIPELINE_BIND_POINT_COMPUTE, vlk_ppls[0]);
  vkCmdBindDescriptorSets(vlk_cb, VK_PIPELINE_BIND_POINT_COMPUTE, vlk_pls[0], 0, 1, vlk_dsets, 0, NULL);
  vkCmdDispatch(vlk_cb, 1, 1, 1);
  vlk_end_command_buffer();
  vlk_submit();
  vkDeviceWaitIdle(vlk_dev());

  float * mem;
  _(vkMapMemory(vlk_dev(), vlk_mems[1], 0, sizeof(float), 0, (void **)&mem));
  printf("output: %f\n", *mem);
  vkUnmapMemory(vlk_dev(), vlk_mems[1]);

  for (int i = 0; i < 1; i++) vkDestroyDescriptorSetLayout(vlk_dev(), vlk_dsls[i], NULL);
  for (int i = 0; i < 1; i++) vkDestroyDescriptorPool(vlk_dev(), vlk_dpools[i], NULL);
  for (int i = 0; i < 1; i++) vkDestroyPipelineLayout(vlk_dev(), vlk_pls[i], NULL);
  for (int i = 0; i < 1; i++) vkDestroyPipeline(vlk_dev(), vlk_ppls[i], NULL);
  for (int i = 0; i < 2; i++) vkDestroyBuffer(vlk_dev(), vlk_bufs[i], NULL);
  for (int i = 0; i < 2; i++) vkFreeMemory(vlk_dev(), vlk_mems[i], NULL);
  vkDestroyCommandPool(vlk_dev(), vlk_cpool, NULL);
  vkDestroyDevice(vlk_dev(), NULL);
  vkDestroyInstance(volkGetLoadedInstance(), NULL);
}
