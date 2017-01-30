#pragma once
#include <memory>
#include <vector>
#include <pumex/Export.h>
#include <vulkan/vulkan.h>

namespace pumex
{

// collection of helper functions to create Vulkan buffers

class Device;

struct PUMEX_EXPORT NBufferMemory
{
  NBufferMemory(VkBufferUsageFlags usageFlags, VkDeviceSize size, void* data = nullptr);
  // input data
  VkBufferUsageFlags usageFlags             = 0;
  VkDeviceSize size                         = 0;
  void* data                                = nullptr;
  // output data
  VkBuffer buffer                           = VK_NULL_HANDLE;
  VkDeviceSize memoryOffset                 = 0;
  VkMemoryRequirements memoryRequirements   = {};
};

// allocate memory for one buffer
PUMEX_EXPORT VkDeviceSize createBuffer(std::shared_ptr<pumex::Device> device, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBuffer* buffer, VkDeviceMemory* memory, void* data = nullptr);
PUMEX_EXPORT void destroyBuffer(std::shared_ptr<pumex::Device> device, VkBuffer buffer, VkDeviceMemory memory);
PUMEX_EXPORT void destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory);

// allocate common memory for many buffers at once, return size of the allocated memory
PUMEX_EXPORT VkDeviceSize createBuffers(std::shared_ptr<pumex::Device> device, std::vector<NBufferMemory>& multiBuffer, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceMemory* memory);
PUMEX_EXPORT void destroyBuffers(std::shared_ptr<pumex::Device> device, std::vector<NBufferMemory>& multiBuffer, VkDeviceMemory memory);
PUMEX_EXPORT void destroyBuffers(VkDevice device, std::vector<NBufferMemory>& multiBuffer, VkDeviceMemory memory);

}