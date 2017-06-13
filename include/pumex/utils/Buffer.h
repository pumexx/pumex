//
// Copyright(c) 2017 Pawe³ Ksiê¿opolski ( pumexx )
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

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
PUMEX_EXPORT VkDeviceSize createBuffer(std::shared_ptr<Device> device, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBuffer* buffer, VkDeviceMemory* memory, void* data = nullptr);
PUMEX_EXPORT void destroyBuffer(std::shared_ptr<Device> device, VkBuffer buffer, VkDeviceMemory memory);
PUMEX_EXPORT void destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory);

// allocate common memory for many buffers at once, return size of the allocated memory
PUMEX_EXPORT VkDeviceSize createBuffers(std::shared_ptr<Device> device, std::vector<NBufferMemory>& multiBuffer, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceMemory* memory);
PUMEX_EXPORT void destroyBuffers(std::shared_ptr<Device> device, std::vector<NBufferMemory>& multiBuffer, VkDeviceMemory memory);
PUMEX_EXPORT void destroyBuffers(VkDevice device, std::vector<NBufferMemory>& multiBuffer, VkDeviceMemory memory);

}