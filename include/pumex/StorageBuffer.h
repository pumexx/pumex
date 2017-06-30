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
#include <unordered_map>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/Pipeline.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

// Simple storage buffer for storing a vector of C++ structs on EACH of the Vulkan devices

namespace pumex
{

template <typename T>
class StorageBuffer : public DescriptorSetSource
{
public:
  StorageBuffer()                                = delete;
  explicit StorageBuffer(std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0 );
  explicit StorageBuffer(const T& data, std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0);
  StorageBuffer(const StorageBuffer&)            = delete;
  StorageBuffer& operator=(const StorageBuffer&) = delete;
  ~StorageBuffer();

  inline void                    set(const std::vector<T>& data);
  inline const std::vector<T>&   get() const;
  void                           getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const override;
  void                           setDirty();
  void                           validate(Device* device, CommandPool* commandPool, VkQueue queue);

  inline void setActiveIndex(uint32_t index);
  inline uint32_t getActiveIndex() const;

private:
  struct PerDeviceData
  {
    PerDeviceData(uint32_t ac)
    {
      dirty.resize(ac, true);
      storageBuffer.resize(ac, VK_NULL_HANDLE);
      memoryBlock.resize(ac, DeviceMemoryBlock());
    }

    std::vector<bool>               dirty;
    std::vector<VkBuffer>           storageBuffer;
    std::vector<DeviceMemoryBlock>  memoryBlock;
  };

  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
  std::vector<T>                              storageData;
  std::weak_ptr<DeviceMemoryAllocator>        allocator;
  VkBufferUsageFlagBits                       additionalFlags;
  uint32_t                                    activeCount;
  uint32_t                                    activeIndex = 0;

};

template <typename T>
StorageBuffer<T>::StorageBuffer(std::weak_ptr<DeviceMemoryAllocator> a, uint32_t ac, VkBufferUsageFlagBits af)
  : allocator{ a }, additionalFlags{ af }, activeCount{ ac }
{
}

template <typename T>
StorageBuffer<T>::StorageBuffer(const T& data, std::weak_ptr<DeviceMemoryAllocator> a, uint32_t ac, VkBufferUsageFlagBits af)
  : storageData(data), allocator{ a }, additionalFlags{ af }, activeCount{ ac }
{
}

template <typename T>
StorageBuffer<T>::~StorageBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  for (auto& pdd : perDeviceData)
  {
    for (uint32_t i = 0; i < activeCount; ++i)
    {
      vkDestroyBuffer(pdd.first, pdd.second.storageBuffer[i], nullptr);
      alloc->deallocate(pdd.first, pdd.second.memoryBlock[i]);
    }
  }
}


template <typename T>
void StorageBuffer<T>::set(const std::vector<T>& data)
{
  if(!data.empty())
    storageData = data;
  else
  {
    storageData.resize(0);
    storageData.push_back(T());
  }
  setDirty();
}

template <typename T>
const std::vector<T>& StorageBuffer<T>::get() const
{
  return storageData;
}

template <typename T>
void StorageBuffer<T>::getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "StorageBuffer<T>::getDescriptorBufferInfo : storage buffer was not validated");

  values.push_back(DescriptorSetValue(pddit->second.storageBuffer[index % activeCount], 0, sizeof(T) * storageData.size()));
}

template <typename T>
void StorageBuffer<T>::setDirty()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    for(uint32_t i=0; i<activeCount; ++i)
      pdd.second.dirty[i] = true;
}

template <typename T>
void StorageBuffer<T>::validate(Device* device, CommandPool* commandPool, VkQueue queue)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData(activeCount) }).first;
  if (!pddit->second.dirty[activeIndex])
    return;
  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  if (pddit->second.storageBuffer[activeIndex] != VK_NULL_HANDLE  && pddit->second.memoryBlock[activeIndex].alignedSize < sizeof(T)*storageData.size())
  {
    vkDestroyBuffer(pddit->first, pddit->second.storageBuffer[activeIndex], nullptr);
    alloc->deallocate(pddit->first, pddit->second.memoryBlock[activeIndex]);
    pddit->second.storageBuffer[activeIndex] = VK_NULL_HANDLE;
    pddit->second.memoryBlock[activeIndex] = DeviceMemoryBlock();
  }

  bool memoryIsLocal = ((alloc->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (pddit->second.storageBuffer[activeIndex] == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
      bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | additionalFlags | (memoryIsLocal? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
      bufferCreateInfo.size  = std::max<VkDeviceSize>(1, sizeof(T)*storageData.size());
    VK_CHECK_LOG_THROW(vkCreateBuffer(device->device, &bufferCreateInfo, nullptr, &pddit->second.storageBuffer[activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->device, pddit->second.storageBuffer[activeIndex], &memReqs);
    pddit->second.memoryBlock[activeIndex] = alloc->allocate(device, memReqs);
    CHECK_LOG_THROW(pddit->second.memoryBlock[activeIndex].alignedSize == 0, "Cannot create SBO");
    alloc->bindBufferMemory(device, pddit->second.storageBuffer[activeIndex], pddit->second.memoryBlock[activeIndex].alignedOffset);

    notifyDescriptorSets();
  }
  if (storageData.size() > 0)
  {
    if (memoryIsLocal)
    {
      std::shared_ptr<StagingBuffer> stagingBuffer = device->acquireStagingBuffer(storageData.data(), sizeof(T)*storageData.size());
      auto staggingCommandBuffer = device->beginSingleTimeCommands(commandPool);
      VkBufferCopy copyRegion{};
      copyRegion.size = sizeof(T)*storageData.size();
      staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, pddit->second.storageBuffer[activeIndex], copyRegion);
      device->endSingleTimeCommands(staggingCommandBuffer, queue);
      device->releaseStagingBuffer(stagingBuffer);
    }
    else
    {
      alloc->copyToDeviceMemory(device, pddit->second.memoryBlock[activeIndex].alignedOffset, storageData.data(), sizeof(T)*storageData.size(), 0);
    }
  }
  pddit->second.dirty[activeIndex] = false;
}

template <typename T>
void StorageBuffer<T>::setActiveIndex(uint32_t index) { activeIndex = index % activeCount; }
template <typename T>
uint32_t StorageBuffer<T>::getActiveIndex() const { return activeIndex; }


}
