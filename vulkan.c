#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

#define VOLK_IMPLEMENTATION
#include "volk.h"

#include "Vulkan-Headers/include/vulkan/vulkan_core.h"

VkInstance vlk_create_instance() {
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
  assert(VK_SUCCESS == vkCreateInstance(&info, NULL, &res));
  return res;
}

int main() {
  assert(VK_SUCCESS == volkInitialize());

  VkInstance inst = vlk_create_instance();
  volkLoadInstance(inst);

  vkDestroyInstance(inst, NULL);
}
