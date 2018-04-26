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
#include <unordered_map>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/Resource.h>
#include <pumex/RenderContext.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

// Simple storage buffer for storing a vector of C++ structs on EACH of the Vulkan devices

namespace pumex
{

template <typename T>
class StorageBuffer : public Resource
{
public:
  StorageBuffer()                                = delete;
  explicit StorageBuffer(std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlags additionalFlags = 0, PerObjectBehaviour perObjectBehaviour = pbPerDevice, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage);
  StorageBuffer(const StorageBuffer&)            = delete;
  StorageBuffer& operator=(const StorageBuffer&) = delete;
  virtual ~StorageBuffer();

  inline void                       set(const std::vector<T>& data);
  inline void                       set(Surface* surface, const std::vector<T>& data);
  inline const std::vector<T>&      get() const;
  inline const std::vector<T>&      get(Surface* surface);

  std::pair<bool, VkDescriptorType> getDefaultDescriptorType() override;
  void                              validate(const RenderContext& renderContext) override;
  void                              invalidate() override;
  DescriptorSetValue                getDescriptorSetValue(const RenderContext& renderContext) override;

  VkBuffer                          getHandleBuffer(const RenderContext& renderContext);

private:
  template<typename X>
  struct StorageBufferInternal
  {
    StorageBufferInternal()
      : storageBuffer(VK_NULL_HANDLE), memoryBlock()
    {}
    VkBuffer           storageBuffer;
    DeviceMemoryBlock  memoryBlock;
    std::vector<X>     storageData;
  };

  std::unordered_map<void*, PerObjectData<StorageBufferInternal<T>>> perObjectData;
  std::vector<T>                                                     storageData;
  std::shared_ptr<DeviceMemoryAllocator>                             allocator;
  VkBufferUsageFlags                                                 additionalFlags;
};

template <typename T>
StorageBuffer<T>::StorageBuffer(std::shared_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlags af, PerObjectBehaviour pob, SwapChainImageBehaviour scib)
  : Resource{ pob, scib }, allocator { a }, additionalFlags{ af }
{
  if(perObjectBehaviour == pbPerDevice)
    storageData.push_back(T());
}

template <typename T>
StorageBuffer<T>::~StorageBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
  {
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
    {
      vkDestroyBuffer(pdd.second.device, pdd.second.data[i].storageBuffer, nullptr);
      allocator->deallocate(pdd.second.device, pdd.second.data[i].memoryBlock);
    }
  }
}

template <typename T>
void StorageBuffer<T>::set(const std::vector<T>& data)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (perObjectBehaviour == pbPerDevice)
  {
    if (!data.empty())
      storageData = data;
    else
    {
      storageData.resize(0);
      storageData.push_back(T());
    }
  }
  else
  {
    for (auto& pdd : perObjectData)
    {
      if (!data.empty())
        pdd.second.data[0].storageData = data;
      else
      {
        pdd.second.data[0].storageData.resize(0);
        pdd.second.data[0].storageData.push_back(T());
      }
    }
  }
  invalidate();
}

template <typename T>
void StorageBuffer<T>::set(Surface* surface, const std::vector<T>& data)
{
  CHECK_LOG_THROW(perObjectBehaviour != pbPerSurface, "Cannot set data per surface for this storage buffer");
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find((void*)(surface->surface));
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ (void*)(surface->surface), PerObjectData<StorageBufferInternal<T>>(surface->device.lock()->device, surface->surface, activeCount) }).first;
  if (!data.empty())
    pddit->second.data[0].storageData = data;
  else
  {
    pddit->second.data[0].storageData.resize(0);
    pddit->second.data[0].storageData.push_back(T());
  }
  pddit->second.invalidate();
  invalidateDescriptors();
}

template <typename T>
const std::vector<T>& StorageBuffer<T>::get() const
{
  std::lock_guard<std::mutex> lock(mutex);
  if (perObjectBehaviour == pbPerSurface)
  {
    auto pddit = begin(perObjectData);
    if (pddit == end(perObjectData))
      return std::vector<T>();
    return pddit->second.data[0].storageData;
  }
  return storageData;
}

template <typename T>
const std::vector<T>& StorageBuffer<T>::get(Surface* surface)
{
  CHECK_LOG_THROW(perObjectBehaviour != pbPerSurface, "Cannot get data per surface for this storage buffer");
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find((void*)(surface->surface));
  if (pddit == end(perObjectData))
    return std::vector<T>();
  return pddit->second.data[0].storageData;
}

template <typename T>
std::pair<bool, VkDescriptorType> StorageBuffer<T>::getDefaultDescriptorType()
{
  return{ true,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
}

template <typename T>
void StorageBuffer<T>::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (swapChainImageBehaviour == swForEachImage && renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKey(renderContext, perObjectBehaviour);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, PerObjectData<StorageBufferInternal<T>>(renderContext) }).first;
  if(perObjectBehaviour == pbPerSurface && pddit->second.data[0].storageData.empty())
    pddit->second.data[0].storageData.push_back(T());
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  const std::vector<T>& sData = (perObjectBehaviour == pbPerDevice) ? storageData : pddit->second.data[0].storageData;

  if (pddit->second.data[activeIndex].storageBuffer != VK_NULL_HANDLE  && pddit->second.data[activeIndex].memoryBlock.alignedSize < sizeof(T)*sData.size())
  {
    vkDestroyBuffer(pddit->second.device, pddit->second.data[activeIndex].storageBuffer, nullptr);
    allocator->deallocate(pddit->second.device, pddit->second.data[activeIndex].memoryBlock);
    pddit->second.data[activeIndex].storageBuffer = VK_NULL_HANDLE;
    pddit->second.data[activeIndex].memoryBlock = DeviceMemoryBlock();
  }

  bool memoryIsLocal = ((allocator->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (pddit->second.data[activeIndex].storageBuffer == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | additionalFlags | (memoryIsLocal ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
    bufferCreateInfo.size = std::max<VkDeviceSize>(1, sizeof(T)*sData.size());
    VK_CHECK_LOG_THROW(vkCreateBuffer(pddit->second.device, &bufferCreateInfo, nullptr, &pddit->second.data[activeIndex].storageBuffer), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(pddit->second.device, pddit->second.data[activeIndex].storageBuffer, &memReqs);
    pddit->second.data[activeIndex].memoryBlock = allocator->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(pddit->second.data[activeIndex].memoryBlock.alignedSize == 0, "Cannot create SBO");
    allocator->bindBufferMemory(renderContext.device, pddit->second.data[activeIndex].storageBuffer, pddit->second.data[activeIndex].memoryBlock.alignedOffset);

    invalidateCommandBuffers();
  }
  if (sData.size() > 0)
  {
    if (memoryIsLocal)
    {
      std::shared_ptr<StagingBuffer> stagingBuffer = renderContext.device->acquireStagingBuffer(sData.data(), sizeof(T)*sData.size());
      auto staggingCommandBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
      VkBufferCopy copyRegion{};
      copyRegion.size = sizeof(T)*sData.size();
      staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, pddit->second.data[activeIndex].storageBuffer, copyRegion);
      renderContext.device->endSingleTimeCommands(staggingCommandBuffer, renderContext.queue);
      renderContext.device->releaseStagingBuffer(stagingBuffer);
    }
    else
    {
      allocator->copyToDeviceMemory(renderContext.device, pddit->second.data[activeIndex].memoryBlock.alignedOffset, sData.data(), sizeof(T)*sData.size(), 0);
    }
  }
  pddit->second.valid[activeIndex] = true;
}

template <typename T>
void StorageBuffer<T>::invalidate()
{
  for (auto& pdd : perObjectData)
    pdd.second.invalidate();
  invalidateDescriptors();
}

template <typename T>
DescriptorSetValue StorageBuffer<T>::getDescriptorSetValue(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(getKey(renderContext, perObjectBehaviour));
  CHECK_LOG_THROW(pddit == end(perObjectData), "StorageBuffer<T>::getDescriptorSetValue() : storage buffer was not validated");
  const std::vector<T>& sData = (perObjectBehaviour == pbPerDevice) ? storageData : pddit->second.data[0].storageData;
  return DescriptorSetValue(pddit->second.data[renderContext.activeIndex % activeCount].storageBuffer, 0, uglyGetSize(sData));
}

template <typename T>
VkBuffer StorageBuffer<T>::getHandleBuffer(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(getKey(renderContext, perObjectBehaviour));
  if (it == end(perObjectData))
    return VK_NULL_HANDLE;
  return it->second.data[renderContext.activeIndex % activeCount].storageBuffer;
}


}
