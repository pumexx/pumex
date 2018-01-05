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
#include <pumex/Surface.h>
#include <pumex/Command.h>
#include <pumex/Pipeline.h>
#include <pumex/RenderContext.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

// Storage buffer that stores different vector of C++ structs per surface

namespace pumex
{

template <typename T>
class StorageBufferPerSurface : public Resource
{
public:
  StorageBufferPerSurface()                                          = delete;
  explicit StorageBufferPerSurface(std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0 );
  StorageBufferPerSurface(const StorageBufferPerSurface&)            = delete;
  StorageBufferPerSurface& operator=(const StorageBufferPerSurface&) = delete;
  ~StorageBufferPerSurface();

  inline void                       set(const std::vector<T>& data);
  inline void                       set(Surface* surface, const std::vector<T>& data);
  inline const std::vector<T>&      get(Surface* surface);

  std::pair<bool, VkDescriptorType> getDefaultDescriptorType() override;
  void                              validate(const RenderContext& renderContext) override;
  void                              invalidate() override;
  void                              getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const override;

  VkBuffer                          getBufferHandle(const RenderContext& renderContext);

private:
  struct PerSurfaceData
  {
    PerSurfaceData(uint32_t ac, VkDevice d)
      : device{ d }
    {
      storageData.push_back(T());
      resize(ac);
    }
    void resize(uint32_t ac)
    {
      valid.resize(ac, false);
      storageBuffer.resize(ac, VK_NULL_HANDLE);
      memoryBlock.resize(ac, DeviceMemoryBlock());
    }
    void invalidate()
    {
      std::fill(valid.begin(), valid.end(), false);
    }

    std::vector<T>                  storageData;
    VkDevice                        device;
    std::vector<bool>               valid;
    std::vector<VkBuffer>           storageBuffer;
    std::vector<DeviceMemoryBlock>  memoryBlock;
  };

  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
  std::shared_ptr<DeviceMemoryAllocator>           allocator;
  VkBufferUsageFlagBits                            additionalFlags;
  uint32_t                                         activeCount = 1;
};

template <typename T>
StorageBufferPerSurface<T>::StorageBufferPerSurface(std::shared_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlagBits af)
  : allocator { a }, additionalFlags{ af }
{
}

template <typename T>
StorageBufferPerSurface<T>::~StorageBufferPerSurface()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perSurfaceData)
  {
    for (uint32_t i = 0; i < pdd.second.storageBuffer.size(); ++i)
    {
      vkDestroyBuffer(pdd.second.device, pdd.second.storageBuffer[i], nullptr);
      allocator->deallocate(pdd.second.device, pdd.second.memoryBlock[i]);
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
    pdd.second.invalidate();
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
  it->second.invalidate();
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
std::pair<bool, VkDescriptorType> StorageBufferPerSurface<T>::getDefaultDescriptorType()
{
  return{ true,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
}

template <typename T>
void StorageBufferPerSurface<T>::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex); 
  if (renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perSurfaceData)
      pdd.second.resize(activeCount);
  }
  auto it = perSurfaceData.find(renderContext.vkSurface);
  if (it == perSurfaceData.end())
    it = perSurfaceData.insert({ renderContext.vkSurface, PerSurfaceData(activeCount, renderContext.vkDevice) }).first;
  if (it->second.valid[renderContext.activeIndex])
    return;

  if (it->second.storageBuffer[renderContext.activeIndex] != VK_NULL_HANDLE  && it->second.memoryBlock[renderContext.activeIndex].alignedSize < sizeof(T)*it->second.storageData.size())
  {
    vkDestroyBuffer(renderContext.vkDevice, it->second.storageBuffer[renderContext.activeIndex], nullptr);
    allocator->deallocate(renderContext.vkDevice, it->second.memoryBlock[renderContext.activeIndex]);
    it->second.storageBuffer[renderContext.activeIndex] = VK_NULL_HANDLE;
    it->second.memoryBlock[renderContext.activeIndex] = DeviceMemoryBlock();
  }

  bool memoryIsLocal = ((allocator->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (it->second.storageBuffer[renderContext.activeIndex] == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | additionalFlags | (memoryIsLocal ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
    bufferCreateInfo.size = std::max<VkDeviceSize>(1, sizeof(T)*it->second.storageData.size());
    VK_CHECK_LOG_THROW(vkCreateBuffer(renderContext.vkDevice, &bufferCreateInfo, nullptr, &it->second.storageBuffer[renderContext.activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(renderContext.vkDevice, it->second.storageBuffer[renderContext.activeIndex], &memReqs);
    it->second.memoryBlock[renderContext.activeIndex] = allocator->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(it->second.memoryBlock[renderContext.activeIndex].alignedSize == 0, "Cannot create SBO");
    allocator->bindBufferMemory(renderContext.device, it->second.storageBuffer[renderContext.activeIndex], it->second.memoryBlock[renderContext.activeIndex].alignedOffset);

    invalidateCommandBuffers();
  }
  if (it->second.storageData.size() > 0)
  {
    if (memoryIsLocal)
    {
      std::shared_ptr<StagingBuffer> stagingBuffer = renderContext.device->acquireStagingBuffer(it->second.storageData.data(), sizeof(T)*it->second.storageData.size());
      auto staggingCommandBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
      VkBufferCopy copyRegion{};
      copyRegion.size = sizeof(T) * it->second.storageData.size();
      staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, it->second.storageBuffer[renderContext.activeIndex], copyRegion);
      renderContext.device->endSingleTimeCommands(staggingCommandBuffer, renderContext.presentationQueue);
      renderContext.device->releaseStagingBuffer(stagingBuffer);
    }
    else
    {
      allocator->copyToDeviceMemory(renderContext.device, it->second.memoryBlock[renderContext.activeIndex].alignedOffset, it->second.storageData.data(), sizeof(T) * it->second.storageData.size(), 0);
    }
  }
  it->second.valid[renderContext.activeIndex] = true;
}

template <typename T>
void StorageBufferPerSurface<T>::getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(renderContext.vkSurface);
  CHECK_LOG_THROW(pddit == perSurfaceData.end(), "StorageBufferPerSurface<T>::getDescriptorBufferInfo : uniform buffer was not validated");

  values.push_back(DescriptorSetValue(pddit->second.storageBuffer[renderContext.activeIndex], 0, sizeof(T)*pddit->second.storageData.size()));
}

template <typename T>
void StorageBufferPerSurface<T>::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perSurfaceData)
    pdd.second.invalidate();
  invalidateDescriptors();
}

template <typename T>
VkBuffer StorageBufferPerSurface<T>::getBufferHandle(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perSurfaceData.find(renderContext.vkSurface);
  if (it == perSurfaceData.end())
    return VK_NULL_HANDLE;
  return it->second.storageBuffer[renderContext.activeIndex];
}

}
