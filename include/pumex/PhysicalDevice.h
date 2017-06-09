#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

struct QueueTraits;

// implementation of a Vulkan physical device
class PUMEX_EXPORT PhysicalDevice
{
public:
  PhysicalDevice()                                 = delete;
  explicit PhysicalDevice(VkPhysicalDevice aDevice);
  PhysicalDevice(const PhysicalDevice&)            = delete;
  PhysicalDevice& operator=(const PhysicalDevice&) = delete;
  virtual ~PhysicalDevice();

  std::vector<uint32_t> matchingFamilyIndices(const QueueTraits& queueDescription);
  uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound = nullptr);
  bool hasExtension(const char* extensionName);

  // physical device
  VkPhysicalDevice                     physicalDevice        = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties           properties;
  VkPhysicalDeviceFeatures             features;
  VkPhysicalDeviceMemoryProperties     memoryProperties;
  std::vector<VkExtensionProperties>   extensions;
  std::vector<VkQueueFamilyProperties> queueFamilyProperties;
  // only when VK_EXT_KHR_display extension is present
  //    std::vector<VkDisplayPropertiesKHR>  displayProperties;

};
  
}
