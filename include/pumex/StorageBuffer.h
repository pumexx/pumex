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
  explicit StorageBuffer(std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0, Resource::SwapChainImageBehaviour swapChainImageBehaviour = Resource::ForEachSwapChainImage);
  explicit StorageBuffer(const T& data, std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0, Resource::SwapChainImageBehaviour swapChainImageBehaviour = Resource::ForEachSwapChainImage);
  StorageBuffer(const StorageBuffer&)            = delete;
  StorageBuffer& operator=(const StorageBuffer&) = delete;
  ~StorageBuffer();

  inline void                       set(const std::vector<T>& data);
  inline const std::vector<T>&      get() const;

  std::pair<bool, VkDescriptorType> getDefaultDescriptorType() override;
  void                              validate(const RenderContext& renderContext) override;
  void                              invalidate() override;
  void                              getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const override;

  VkBuffer                          getBufferHandle(const RenderContext& renderContext);

private:
  struct PerDeviceData
  {
    PerDeviceData(uint32_t ac)
    {
      resize(ac);
    }
    void resize(uint32_t ac)
    {
      valid.resize(ac, false);
      storageBuffer.resize(ac, VK_NULL_HANDLE);
      memoryBlock.resize(ac, DeviceMemoryBlock());
    }

    std::vector<bool>               valid;
    std::vector<VkBuffer>           storageBuffer;
    std::vector<DeviceMemoryBlock>  memoryBlock;
  };

  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
  std::vector<T>                              storageData;
  std::shared_ptr<DeviceMemoryAllocator>      allocator;
  VkBufferUsageFlagBits                       additionalFlags;
  uint32_t                                    activeCount = 1;
};

template <typename T>
StorageBuffer<T>::StorageBuffer(std::shared_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlagBits af, Resource::SwapChainImageBehaviour scib)
  : Resource{ scib }, allocator { a }, additionalFlags{ af }
{
}

template <typename T>
StorageBuffer<T>::StorageBuffer(const T& data, std::shared_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlagBits af, Resource::SwapChainImageBehaviour scib)
  : Resource{ scib }, storageData(data), allocator{ a }, additionalFlags{ af }
{
}

template <typename T>
StorageBuffer<T>::~StorageBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
  {
    for (uint32_t i = 0; i < pdd.second.storageBuffer.size(); ++i)
    {
      vkDestroyBuffer(pdd.first, pdd.second.storageBuffer[i], nullptr);
      allocator->deallocate(pdd.first, pdd.second.memoryBlock[i]);
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
  invalidate();
}

template <typename T>
const std::vector<T>& StorageBuffer<T>::get() const
{
  return storageData;
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
  if (swapChainImageBehaviour == Resource::ForEachSwapChainImage && renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perDeviceData)
      pdd.second.resize(activeCount);
  }
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ renderContext.vkDevice, PerDeviceData(activeCount) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  if (pddit->second.storageBuffer[activeIndex] != VK_NULL_HANDLE  && pddit->second.memoryBlock[activeIndex].alignedSize < sizeof(T)*storageData.size())
  {
    vkDestroyBuffer(pddit->first, pddit->second.storageBuffer[activeIndex], nullptr);
    allocator->deallocate(pddit->first, pddit->second.memoryBlock[activeIndex]);
    pddit->second.storageBuffer[activeIndex] = VK_NULL_HANDLE;
    pddit->second.memoryBlock[activeIndex] = DeviceMemoryBlock();
  }

  bool memoryIsLocal = ((allocator->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (pddit->second.storageBuffer[activeIndex] == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | additionalFlags | (memoryIsLocal ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
    bufferCreateInfo.size = std::max<VkDeviceSize>(1, sizeof(T)*storageData.size());
    VK_CHECK_LOG_THROW(vkCreateBuffer(renderContext.vkDevice, &bufferCreateInfo, nullptr, &pddit->second.storageBuffer[activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(renderContext.vkDevice, pddit->second.storageBuffer[activeIndex], &memReqs);
    pddit->second.memoryBlock[activeIndex] = allocator->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(pddit->second.memoryBlock[activeIndex].alignedSize == 0, "Cannot create SBO");
    allocator->bindBufferMemory(renderContext.device, pddit->second.storageBuffer[activeIndex], pddit->second.memoryBlock[activeIndex].alignedOffset);

    invalidateCommandBuffers();
  }
  if (storageData.size() > 0)
  {
    if (memoryIsLocal)
    {
      std::shared_ptr<StagingBuffer> stagingBuffer = renderContext.device->acquireStagingBuffer(storageData.data(), sizeof(T)*storageData.size());
      auto staggingCommandBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
      VkBufferCopy copyRegion{};
      copyRegion.size = sizeof(T)*storageData.size();
      staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, pddit->second.storageBuffer[activeIndex], copyRegion);
      renderContext.device->endSingleTimeCommands(staggingCommandBuffer, renderContext.queue);
      renderContext.device->releaseStagingBuffer(stagingBuffer);
    }
    else
    {
      allocator->copyToDeviceMemory(renderContext.device, pddit->second.memoryBlock[activeIndex].alignedOffset, storageData.data(), sizeof(T)*storageData.size(), 0);
    }
  }
  pddit->second.valid[activeIndex] = true;
}

template <typename T>
void StorageBuffer<T>::getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "StorageBuffer<T>::getDescriptorBufferInfo : storage buffer was not validated");

  values.push_back(DescriptorSetValue(pddit->second.storageBuffer[renderContext.activeIndex % activeCount], 0, sizeof(T) * storageData.size()));
}

template <typename T>
void StorageBuffer<T>::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    for(uint32_t i=0; i<pdd.second.valid.size(); ++i)
      pdd.second.valid[i] = false;
  invalidateDescriptors();
}

template <typename T>
VkBuffer StorageBuffer<T>::getBufferHandle(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perDeviceData.find(renderContext.vkDevice);
  if (it == perDeviceData.end())
    return VK_NULL_HANDLE;
  return it->second.storageBuffer[renderContext.activeIndex % activeCount];
}


}
