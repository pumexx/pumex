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

// Storage buffer that stores different vector of C++ structs per surface

namespace pumex
{

template <typename T>
class StorageBufferPerSurface : public DescriptorSetSource
{
public:
  StorageBufferPerSurface()                                          = delete;
  explicit StorageBufferPerSurface(std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0 );
  StorageBufferPerSurface(const StorageBufferPerSurface&)            = delete;
  StorageBufferPerSurface& operator=(const StorageBufferPerSurface&) = delete;
  ~StorageBufferPerSurface();

  inline void                    set(const std::vector<T>& data);
  inline void                    set(Surface* surface, const std::vector<T>& data);
  inline const std::vector<T>&   get(Surface* surface);
  void                           getDescriptorSetValues(VkSurfaceKHR surface, uint32_t index, std::vector<DescriptorSetValue>& values) const override;
  void                           setDirty();
  void                           validate(Surface* surface);
  VkBuffer                       getBufferHandle(Surface* surface);

  inline void setActiveIndex(uint32_t index);
  inline uint32_t getActiveIndex() const;

private:
  struct PerSurfaceData
  {
    PerSurfaceData(uint32_t ac, VkDevice d)
      : device{ d }
    {
      storageData.push_back(T());
      dirty.resize(ac, true);
      storageBuffer.resize(ac, VK_NULL_HANDLE);
      memoryBlock.resize(ac, DeviceMemoryBlock());
    }
    void setDirty()
    {
      for (auto& d : dirty)
        d = true;
    }

    std::vector<T>                  storageData;
    VkDevice                        device;
    std::vector<bool>               dirty;
    std::vector<VkBuffer>           storageBuffer;
    std::vector<DeviceMemoryBlock>  memoryBlock;
  };

  mutable std::mutex                               mutex;
  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
  std::weak_ptr<DeviceMemoryAllocator>             allocator;
  VkBufferUsageFlagBits                            additionalFlags;
  uint32_t                                         activeCount;
  uint32_t                                         activeIndex = 0;

};

template <typename T>
StorageBufferPerSurface<T>::StorageBufferPerSurface(std::weak_ptr<DeviceMemoryAllocator> a, uint32_t ac, VkBufferUsageFlagBits af)
  : allocator{ a }, additionalFlags{ af }, activeCount{ ac }
{
}

template <typename T>
StorageBufferPerSurface<T>::~StorageBufferPerSurface()
{
  std::lock_guard<std::mutex> lock(mutex);
  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  for (auto& pdd : perSurfaceData)
  {
    for (uint32_t i = 0; i < activeCount; ++i)
    {
      vkDestroyBuffer(pdd.second.device, pdd.second.storageBuffer[i], nullptr);
      alloc->deallocate(pdd.second.device, pdd.second.memoryBlock[i]);
    }
  }
}

template <typename T>
void StorageBufferPerSurface<T>::set(const std::vector<T>& data)
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perSurfaceData)
  {
    if(!data.empty())
      pdd.second.storageData = data;
    else
    {
      pdd.second.storageData.resize(0);
      pdd.second.storageData.push_back(T());
    }
    pdd.second.setDirty();
  }
}

template <typename T>
void StorageBufferPerSurface<T>::set(Surface* surface, const std::vector<T>& data)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perSurfaceData.find(surface->surface);
  if (it == perSurfaceData.end())
    it = perSurfaceData.insert({ surface->surface, PerSurfaceData(activeCount, surface->device.lock()->device) }).first;
  it->second.storageData = data;
  it->second.setDirty();
}

template <typename T>
const std::vector<T>& StorageBufferPerSurface<T>::get(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perSurfaceData.find(surface->surface);
  if (it == perSurfaceData.end())
    it = perSurfaceData.insert({ surface->surface, PerSurfaceData(activeCount, surface->device.lock()->device) }).first;
  return it->second.storageData;
}

template <typename T>
void StorageBufferPerSurface<T>::getDescriptorSetValues(VkSurfaceKHR surface, uint32_t index, std::vector<DescriptorSetValue>& values) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface);
  CHECK_LOG_THROW(pddit == perSurfaceData.end(), "StorageBufferPerSurface<T>::getDescriptorBufferInfo : uniform buffer was not validated");

  values.push_back(DescriptorSetValue(pddit->second.storageBuffer[index % activeCount], 0, sizeof(T)*pddit->second.storageData.size()));
}

template <typename T>
void StorageBufferPerSurface<T>::setDirty()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    pdd.second->setDirty();
}

template <typename T>
void StorageBufferPerSurface<T>::validate(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  Device* devicePtr = surface->device.lock().get();
  VkDevice device = devicePtr->device;
  auto it = perSurfaceData.find(surface->surface);
  if (it == perSurfaceData.end())
    it = perSurfaceData.insert({ surface->surface, PerSurfaceData(activeCount, device) }).first;
  if (!it->second.dirty[activeIndex])
    return;

  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  if (it->second.storageBuffer[activeIndex] != VK_NULL_HANDLE  && it->second.memoryBlock[activeIndex].alignedSize < sizeof(T)*it->second.storageData.size() )
  {
    vkDestroyBuffer(device, it->second.storageBuffer[activeIndex], nullptr);
    alloc->deallocate(device, it->second.memoryBlock[activeIndex]);
    it->second.storageBuffer[activeIndex] = VK_NULL_HANDLE;
    it->second.memoryBlock[activeIndex]   = DeviceMemoryBlock();
  }

  bool memoryIsLocal = ((alloc->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (it->second.storageBuffer[activeIndex] == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
      bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | additionalFlags | (memoryIsLocal? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
      bufferCreateInfo.size  = std::max<VkDeviceSize>(1, sizeof(T)*it->second.storageData.size());
    VK_CHECK_LOG_THROW(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &it->second.storageBuffer[activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, it->second.storageBuffer[activeIndex], &memReqs);
    it->second.memoryBlock[activeIndex] = alloc->allocate(devicePtr, memReqs);
    CHECK_LOG_THROW(it->second.memoryBlock[activeIndex].alignedSize == 0, "Cannot create SBO");
    alloc->bindBufferMemory(devicePtr, it->second.storageBuffer[activeIndex], it->second.memoryBlock[activeIndex].alignedOffset);

    notifyDescriptorSets();
  }
  if (it->second.storageData.size() > 0)
  {
    if (memoryIsLocal)
    {
      std::shared_ptr<StagingBuffer> stagingBuffer = devicePtr->acquireStagingBuffer(it->second.storageData.data(), sizeof(T)*it->second.storageData.size());
      auto staggingCommandBuffer = devicePtr->beginSingleTimeCommands(surface->commandPool.get());
      VkBufferCopy copyRegion{};
      copyRegion.size = sizeof(T) * it->second.storageData.size();
      staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, it->second.storageBuffer[activeIndex], copyRegion);
      devicePtr->endSingleTimeCommands(staggingCommandBuffer, surface->presentationQueue);
      devicePtr->releaseStagingBuffer(stagingBuffer);
    }
    else
    {
      alloc->copyToDeviceMemory(devicePtr, it->second.memoryBlock[activeIndex].alignedOffset, it->second.storageData.data(), sizeof(T) * it->second.storageData.size(), 0);
    }
  }
  it->second.dirty[activeIndex] = false;
}

template <typename T>
VkBuffer StorageBufferPerSurface<T>::getBufferHandle(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perSurfaceData.find(surface->surface);
  if (it == perSurfaceData.end())
    return VK_NULL_HANDLE;
  return it->second.storageBuffer[activeIndex];
}


template <typename T>
void StorageBufferPerSurface<T>::setActiveIndex(uint32_t index) { activeIndex = index % activeCount; }
template <typename T>
uint32_t StorageBufferPerSurface<T>::getActiveIndex() const { return activeIndex; }


}
