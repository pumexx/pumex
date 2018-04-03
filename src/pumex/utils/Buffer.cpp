//
// Copyright(c) 2017-2018 Pawe³ Ksiê¿opolski ( pumexx )
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

#include <pumex/utils/Buffer.h>
#include <algorithm>
#include <cstring>
#include <pumex/Device.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/utils/Log.h>

namespace pumex
{

VkDeviceSize createBuffer(Device* device, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBuffer * buffer, VkDeviceMemory * memory, void* data)
{
  VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = usageFlags;
    bufferCreateInfo.size  = std::max<VkDeviceSize>(1,size);
  VK_CHECK_LOG_THROW(vkCreateBuffer(device->device, &bufferCreateInfo, nullptr, buffer), "Cannot create buffer");

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device->device, *buffer, &memReqs);

  VkMemoryAllocateInfo memAlloc{};
    memAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.allocationSize  = memReqs.size;
    memAlloc.memoryTypeIndex = device->physical.lock()->getMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);
  VK_CHECK_LOG_THROW(vkAllocateMemory(device->device, &memAlloc, nullptr, memory), "Cannot allocate memory for buffer");

  if (data != nullptr)
  {
    void *mapAddress;
    VK_CHECK_LOG_THROW(vkMapMemory(device->device, *memory, 0, size, 0, &mapAddress), "Cannot map memory");
    std::memcpy(mapAddress, data, size);
    vkUnmapMemory(device->device, *memory);
  }
  VK_CHECK_LOG_THROW(vkBindBufferMemory(device->device, *buffer, *memory, 0), "Cannot bind memory to buffer");

  return memAlloc.allocationSize;
}

void destroyBuffer(Device*  device, VkBuffer buffer, VkDeviceMemory memory)
{
  vkDestroyBuffer(device->device, buffer, nullptr);
  vkFreeMemory(device->device, memory, nullptr);
}

void destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
{
  vkDestroyBuffer(device, buffer, nullptr);
  vkFreeMemory(device, memory, nullptr);
}


NBufferMemory::NBufferMemory(VkBufferUsageFlags uf, VkDeviceSize s, void* d)
  : usageFlags{uf}, size{s}, data{d}
{
}

VkDeviceSize createBuffers(Device* device, std::vector<NBufferMemory>& multiBuffer, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceMemory* memory)
{
  if (multiBuffer.empty())
    return 0;
  VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;

  VkMemoryAllocateInfo memAlloc{};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

  VkDeviceSize memorySize = 0;
  for (auto& buffer : multiBuffer)
  {
    bufferCreateInfo.usage = buffer.usageFlags;
    bufferCreateInfo.size = std::max<VkDeviceSize>(1, buffer.size);

    VK_CHECK_LOG_THROW(vkCreateBuffer(device->device, &bufferCreateInfo, nullptr, &buffer.buffer), "Cannot create buffer");

    vkGetBufferMemoryRequirements(device->device, buffer.buffer, &buffer.memoryRequirements);

    buffer.memoryOffset = memorySize;
    memorySize += buffer.memoryRequirements.size;
  }
  // FIXME - we are taking memory type index from first created buffer
  memAlloc.memoryTypeIndex = device->physical.lock()->getMemoryType(multiBuffer[0].memoryRequirements.memoryTypeBits, memoryPropertyFlags);
  memAlloc.allocationSize = memorySize;
  VK_CHECK_LOG_THROW(vkAllocateMemory(device->device, &memAlloc, nullptr, memory), "Cannot allocate memory for buffer");

  for (auto& buffer : multiBuffer)
  {
    if (buffer.data != nullptr)
    {
      void *mapAddress;
      VK_CHECK_LOG_THROW(vkMapMemory(device->device, *memory, buffer.memoryOffset, buffer.size, 0, &mapAddress), "Cannot map memory");
      std::memcpy(mapAddress, buffer.data, buffer.size);
      vkUnmapMemory(device->device, *memory);
    }
    VK_CHECK_LOG_THROW(vkBindBufferMemory(device->device, buffer.buffer, *memory, buffer.memoryOffset), "Cannot bind memory to buffer");
  }
  return memorySize;
}

void destroyBuffers(Device* device, std::vector<NBufferMemory>& multiBuffer, VkDeviceMemory memory)
{
  for (auto& buffer : multiBuffer)
  {
    vkDestroyBuffer(device->device, buffer.buffer, nullptr);
  }
  vkFreeMemory(device->device, memory, nullptr);
}

void destroyBuffers(VkDevice device, std::vector<NBufferMemory>& multiBuffer, VkDeviceMemory memory)
{
  for (auto& buffer : multiBuffer)
  {
    vkDestroyBuffer(device, buffer.buffer, nullptr);
  }
  vkFreeMemory(device, memory, nullptr);
}

StagingBuffer::StagingBuffer(Device* d, VkDeviceSize s)
  : device{ d->device }
{
  memorySize = createBuffer(d, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, s, &buffer, &memory);
  CHECK_LOG_THROW(memorySize == 0, "Cannot create staging buffer");
}

StagingBuffer::~StagingBuffer()
{
  destroyBuffer(device, buffer, memory);
}

void StagingBuffer::fillBuffer(void* data, VkDeviceSize size)
{
  void *mapAddress;
  VK_CHECK_LOG_THROW(vkMapMemory(device, memory, 0, size, 0, &mapAddress), "Cannot map memory");
  std::memcpy(mapAddress, data, size);
  vkUnmapMemory(device, memory);
}




}