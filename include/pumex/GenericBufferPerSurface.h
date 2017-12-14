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

// Generic Vulkan buffer

namespace pumex
{

template <typename T>
class GenericBufferPerSurface : public Resource, public CommandBufferSource
{
public:
  GenericBufferPerSurface()                                          = delete;
  explicit GenericBufferPerSurface(VkBufferUsageFlagBits usage, std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1);
  GenericBufferPerSurface(const GenericBufferPerSurface&)            = delete;
  GenericBufferPerSurface& operator=(const GenericBufferPerSurface&) = delete;
  ~GenericBufferPerSurface();

  inline void               set(std::shared_ptr<T> data);
  inline                    void set(Surface* surface, std::shared_ptr<T>);
  inline std::shared_ptr<T> get(Surface* surface) const;

  void                      validate(const RenderContext& renderContext) override;
  void                      getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const override;

  void                      setDirty();
  VkBuffer                  getBufferHandle(Surface* surface);

  inline void               setActiveIndex(uint32_t index);
  inline uint32_t           getActiveIndex() const;

private:
  struct PerSurfaceData
  {
    PerSurfaceData(uint32_t ac, VkDevice d)
      : device{ d }
    {
      dirty.resize(ac, true);
      buffer.resize(ac, VK_NULL_HANDLE);
      memoryBlock.resize(ac, DeviceMemoryBlock());
    }
    void setDirty()
    {
      std::fill(dirty.begin(), dirty.end(), true);
    }

    std::shared_ptr<T>              data;
    VkDevice                        device;
    std::vector<bool>               dirty;
    std::vector<VkBuffer>           buffer;
    std::vector<DeviceMemoryBlock>  memoryBlock;
  };

  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
  VkBufferUsageFlagBits                            usage;
  std::weak_ptr<DeviceMemoryAllocator>             allocator;
  uint32_t                                         activeCount;
  uint32_t                                         activeIndex = 0;

};

template <typename T>
GenericBufferPerSurface<T>::GenericBufferPerSurface(VkBufferUsageFlagBits u, std::weak_ptr<DeviceMemoryAllocator> a, uint32_t ac)
  : usage{ u }, allocator { a }, activeCount{ ac }
{
}

template <typename T>
GenericBufferPerSurface<T>::~GenericBufferPerSurface()
{
  std::lock_guard<std::mutex> lock(mutex);
  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  for (auto& pdd : perSurfaceData)
  {
    for (uint32_t i = 0; i < activeCount; ++i)
    {
      vkDestroyBuffer(pdd.second.device, pdd.second.buffer[i], nullptr);
      alloc->deallocate(pdd.second.device, pdd.second.memoryBlock[i]);
    }
  }
}

template <typename T>
void GenericBufferPerSurface<T>::set(std::shared_ptr<T> data)
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perSurfaceData)
    pdd.second.data = data;
  setDirty();
}

template <typename T>
void GenericBufferPerSurface<T>::set(Surface* surface, std::shared_ptr<T> data)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(activeCount,surface->device.lock()->device) }).first;
  pddit->second.data = data;
  pddit->second.setDirty();
}

template <typename T>
std::shared_ptr<T> GenericBufferPerSurface<T>::get(Surface* surface) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  CHECK_LOG_THROW(pddit == perSurfaceData.end(), "GenericBufferPerSurface<T>::get() : generic buffer was not validated");
  return pddit->second.data;
}

template <typename T>
void GenericBufferPerSurface<T>::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);

  auto pddit = perSurfaceData.find(renderContext.vkSurface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ renderContext.vkSurface, PerSurfaceData(activeCount,renderContext.vkDevice) }).first;
  if (!pddit->second.dirty[activeIndex])
    return;
  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  if (pddit->second.buffer[activeIndex] != VK_NULL_HANDLE  && pddit->second.memoryBlock[activeIndex].alignedSize < uglyGetSize(*(pddit->second.data)))
  {
    vkDestroyBuffer(pddit->second.device, pddit->second.buffer[activeIndex], nullptr);
    alloc->deallocate(pddit->second.device, pddit->second.memoryBlock[activeIndex]);
    pddit->second.buffer[activeIndex] = VK_NULL_HANDLE;
    pddit->second.memoryBlock[activeIndex] = DeviceMemoryBlock();
  }

  bool memoryIsLocal = ((alloc->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (pddit->second.buffer[activeIndex] == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = usage | (memoryIsLocal ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
    bufferCreateInfo.size = std::max<VkDeviceSize>(1, uglyGetSize(*(pddit->second.data)));
    VK_CHECK_LOG_THROW(vkCreateBuffer(pddit->second.device, &bufferCreateInfo, nullptr, &pddit->second.buffer[activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(pddit->second.device, pddit->second.buffer[activeIndex], &memReqs);
    pddit->second.memoryBlock[activeIndex] = alloc->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(pddit->second.memoryBlock[activeIndex].alignedSize == 0, "Cannot create a buffer " << usage);
    alloc->bindBufferMemory(renderContext.device, pddit->second.buffer[activeIndex], pddit->second.memoryBlock[activeIndex].alignedOffset);

    notifyDescriptors();
    notifyCommandBuffers(activeIndex);
  }
  if (uglyGetSize(*(pddit->second.data)) > 0)
  {
    if (memoryIsLocal)
    {
      std::shared_ptr<StagingBuffer> stagingBuffer = renderContext.device->acquireStagingBuffer(uglyGetPointer(*(pddit->second.data)), uglyGetSize(*(pddit->second.data)));
      auto staggingCommandBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
      VkBufferCopy copyRegion{};
      copyRegion.size = uglyGetSize(*(pddit->second.data));
      staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, pddit->second.buffer[activeIndex], copyRegion);
      renderContext.device->endSingleTimeCommands(staggingCommandBuffer, renderContext.presentationQueue);
      renderContext.device->releaseStagingBuffer(stagingBuffer);
    }
    else
    {
      alloc->copyToDeviceMemory(renderContext.device, pddit->second.memoryBlock[activeIndex].alignedOffset, uglyGetPointer(*(pddit->second.data)), uglyGetSize(*(pddit->second.data)), 0);
    }
  }
  pddit->second.dirty[activeIndex] = false;
}

template <typename T>
void GenericBufferPerSurface<T>::getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(renderContext.vkSurface);
  CHECK_LOG_THROW(pddit == perSurfaceData.end(), "GenericBufferPerSurface<T>::getDescriptorSetValues() : generic buffer was not validated");

  values.push_back( DescriptorSetValue(pddit->second.buffer[renderContext.activeIndex % activeCount], 0, uglyGetSize(*(pddit->second.data))));
}

template <typename T>
void GenericBufferPerSurface<T>::setDirty()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perSurfaceData)
    pdd.second.setDirty();
}

template <typename T>
VkBuffer GenericBufferPerSurface<T>::getBufferHandle(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perSurfaceData.find(surface->surface);
  if (it == perSurfaceData.end())
    return VK_NULL_HANDLE;
  return it->second.buffer[activeIndex];
}


template <typename T>
void GenericBufferPerSurface<T>::setActiveIndex(uint32_t index) { activeIndex = index % activeCount; }
template <typename T>
uint32_t GenericBufferPerSurface<T>::getActiveIndex() const { return activeIndex; }


}
