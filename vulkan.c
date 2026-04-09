#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

#define VOLK_IMPLEMENTATION
#include "volk.h"

#include "Vulkan-Headers/include/vulkan/vulkan_core.h"

#define _(X) assert(VK_SUCCESS == (X));

VkPhysicalDevice vlk_pd;
unsigned vlk_qf;
VkPipeline vlk_ppls[1];

void vlk_create_instance() {
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

void vlk_find_physical_device() {
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

void vlk_create_device() {
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

int main() {
  _(volkInitialize());

  vlk_create_instance();
  vlk_find_physical_device();
  vlk_create_device();

  vkDestroyDevice(volkGetLoadedDevice(), NULL);
  vkDestroyInstance(volkGetLoadedInstance(), NULL);
}
