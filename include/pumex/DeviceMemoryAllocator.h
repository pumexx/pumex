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

#pragma once
#include <memory>
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

class Device;

struct PUMEX_EXPORT DeviceMemoryBlock
{
  DeviceMemoryBlock();
  DeviceMemoryBlock(VkDeviceMemory memory, VkDeviceSize realOffset, VkDeviceSize alignedOffset, VkDeviceSize realSize, VkDeviceSize alignedSize);

  VkDeviceMemory memory;
  VkDeviceSize   realOffset;
  VkDeviceSize   alignedOffset;
  VkDeviceSize   realSize;
  VkDeviceSize   alignedSize;
};

struct FreeBlock
{
  FreeBlock(VkDeviceSize offset, VkDeviceSize size);
  VkDeviceSize offset;
  VkDeviceSize size;
};

class PUMEX_EXPORT AllocationStrategy
{
public:
  virtual DeviceMemoryBlock allocate(VkDeviceMemory storageMemory, std::list<FreeBlock>& freeBlocks, VkMemoryRequirements memoryRequirements) = 0;
  virtual void deallocate(std::list<FreeBlock>& freeBlocks, const DeviceMemoryBlock& block) = 0;
};

// DeviceMemoryAllocator is a class that enables user to store different data ( Vulkan buffers and images ) in a single block of
// GPU/host memory allocated by vkAllocateMemory(). User may define what type of memory he wants from the Vulkan ( VkMemoryPropertyFlags ),
// how much of that memory should be allocated and what allocation strategy to use when allocating/deallocating memory.
// For now only one strategy is implemented : first fit allocation.
class PUMEX_EXPORT DeviceMemoryAllocator
{
public:
  enum EnumStrategy { FIRST_FIT };
  DeviceMemoryAllocator()                                        = delete;
  explicit DeviceMemoryAllocator(VkMemoryPropertyFlags propertyFlags, VkDeviceSize size, EnumStrategy strategy);
  DeviceMemoryAllocator(const DeviceMemoryAllocator&)            = delete;
  DeviceMemoryAllocator& operator=(const DeviceMemoryAllocator&) = delete;
  DeviceMemoryAllocator(DeviceMemoryAllocator&&)                 = delete;
  DeviceMemoryAllocator& operator=(DeviceMemoryAllocator&&)      = delete;
  ~DeviceMemoryAllocator();


  DeviceMemoryBlock            allocate(Device* device, VkMemoryRequirements memoryRequirements);
  void                         deallocate(VkDevice device, const DeviceMemoryBlock& block);

  // method that makes vkMapMemory() / std::memcpy() / vkUnmapMemory() behind a mutex - use it instead of performing is yourself
  void                         copyToDeviceMemory(Device* device, VkDeviceSize offset, const void* data, VkDeviceSize size, VkMemoryMapFlags flags);
  void                         bindBufferMemory(Device* device, VkBuffer buffer, VkDeviceSize offset);

  inline VkMemoryPropertyFlags getMemoryPropertyFlags() const;
  inline VkDeviceSize          getMemorySize() const;

protected:
  struct PerDeviceData
  {
    PerDeviceData()
    {
    }
    VkDeviceMemory       storageMemory = VK_NULL_HANDLE;
    std::list<FreeBlock> freeBlocks;
  };
  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
  VkMemoryPropertyFlags                       propertyFlags;
  VkDeviceSize                                size;
  std::unique_ptr<AllocationStrategy>         allocationStrategy;
};

VkMemoryPropertyFlags DeviceMemoryAllocator::getMemoryPropertyFlags() const { return propertyFlags; }
VkDeviceSize          DeviceMemoryAllocator::getMemorySize() const { return size; }

class PUMEX_EXPORT FirstFitAllocationStrategy : public AllocationStrategy
{
public:
  FirstFitAllocationStrategy();

  DeviceMemoryBlock allocate(VkDeviceMemory storageMemory, std::list<FreeBlock>& freeBlocks, VkMemoryRequirements memoryRequirements) override;
  void              deallocate(std::list<FreeBlock>& freeBlocks, const DeviceMemoryBlock& block) override;
};

// OK, last time I read a book about C++ templates about seven years ago, so this code may look ugly in 2017
template<typename T> size_t uglyGetSize(const T& t) { return sizeof(T); }
template<typename T> size_t uglyGetSize(const std::vector<T>& t) { return t.size() * sizeof(T); }
template<typename T> T*     uglyGetPointer(T& t) { return std::addressof(t); }
template<typename T> T*     uglyGetPointer(std::vector<T>& t) { return t.data(); }

}
