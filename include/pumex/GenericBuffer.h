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
#include <mutex>
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/Resource.h>
#include <pumex/RenderContext.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

// Generic Vulkan buffer

namespace pumex
{

template <typename T>
class GenericBuffer : public Resource, public CommandBufferSource
{
public:
  GenericBuffer()                                = delete;
  explicit GenericBuffer(std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlags usage, PerObjectBehaviour perObjectBehaviour = pbPerDevice, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage);
  GenericBuffer(const GenericBuffer&)            = delete;
  GenericBuffer& operator=(const GenericBuffer&) = delete;
  virtual ~GenericBuffer();

  inline void               set(std::shared_ptr<T> data);
  inline void               set(Surface* surface, std::shared_ptr<T>);
  inline std::shared_ptr<T> get() const;
  inline std::shared_ptr<T> get(Surface* surface) const;

  void               validate(const RenderContext& renderContext) override;
  void               invalidate() override;
  DescriptorSetValue getDescriptorSetValue(const RenderContext& renderContext) override;

  VkBuffer           getHandleBuffer(const RenderContext& renderContext);

private:
  struct GenericBufferInternal
  {
    GenericBufferInternal()
      : buffer(VK_NULL_HANDLE), memoryBlock()
    {}
    VkBuffer           buffer;
    DeviceMemoryBlock  memoryBlock;
  };
  typedef PerObjectData<GenericBufferInternal, std::shared_ptr<T>> GenericBufferData;

  std::unordered_map<void*, GenericBufferData> perObjectData;
  std::shared_ptr<T>                           data;
  std::shared_ptr<DeviceMemoryAllocator>       allocator;
  VkBufferUsageFlags                           usage;
};

template <typename T>
GenericBuffer<T>::GenericBuffer(std::shared_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlags u, PerObjectBehaviour pob, SwapChainImageBehaviour scib)
  : Resource{ pob, scib }, allocator{ a }, usage{ u }
{
}

template <typename T>
GenericBuffer<T>::~GenericBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
  {
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
    {
      vkDestroyBuffer(pdd.second.device, pdd.second.data[i].buffer, nullptr);
      allocator->deallocate(pdd.second.device, pdd.second.data[i].memoryBlock);
    }
  }
}

template <typename T>
void GenericBuffer<T>::set(std::shared_ptr<T> d)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (perObjectBehaviour == pbPerDevice)
    data = d;
  else
  {
    for (auto& pdd : perObjectData)
      pdd.second.commonData = d;
  }
  for (auto& pdd : perObjectData)
    pdd.second.invalidate();
}

template <typename T>
void GenericBuffer<T>::set(Surface* surface, std::shared_ptr<T> d)
{
  CHECK_LOG_THROW(perObjectBehaviour != pbPerSurface, "Cannot set data per surface for this generic buffer");
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find((void*)(surface->surface));
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ (void*)(surface->surface), GenericBufferData(surface->device.lock()->device, surface->surface, activeCount) }).first;
  pddit->second.commonData = d;
  pddit->second.invalidate();
  invalidateDescriptors();
}

template <typename T>
std::shared_ptr<T> GenericBuffer<T>::get() const
{
  std::lock_guard<std::mutex> lock(mutex);
  if (perObjectBehaviour == pbPerSurface)
  {
    auto pddit = begin(perObjectData);
    if (pddit == end(perObjectData))
      return std::shared_ptr<T>();
    return pddit->second.commonData;
  }
  return data;
}

template <typename T>
std::shared_ptr<T> GenericBuffer<T>::get(Surface* surface) const
{
  CHECK_LOG_THROW(perObjectBehaviour != pbPerSurface, "Cannot get data per surface for this generic buffer");
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = begin(perObjectData);
  if (pddit == end(perObjectData))
    return std::shared_ptr<T>();
  return pddit->second.commonData;
}

template <typename T>
void GenericBuffer<T>::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if ( swapChainImageBehaviour == swForEachImage && renderContext.imageCount > activeCount )
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKey(renderContext, perObjectBehaviour);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, GenericBufferData(renderContext) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  std::shared_ptr<T> sData = (perObjectBehaviour == pbPerDevice) ? data : pddit->second.commonData;

  if (pddit->second.data[activeIndex].buffer != VK_NULL_HANDLE  && pddit->second.data[activeIndex].memoryBlock.alignedSize < uglyGetSize(*sData))
  {
    vkDestroyBuffer(pddit->second.device, pddit->second.data[activeIndex].buffer, nullptr);
    allocator->deallocate(pddit->second.device, pddit->second.data[activeIndex].memoryBlock);
    pddit->second.data[activeIndex].buffer = VK_NULL_HANDLE;
    pddit->second.data[activeIndex].memoryBlock = DeviceMemoryBlock();
  }

  bool memoryIsLocal = ((allocator->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (pddit->second.data[activeIndex].buffer == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
      bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferCreateInfo.usage = usage | (memoryIsLocal ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
      bufferCreateInfo.size  = std::max<VkDeviceSize>(1, uglyGetSize(*sData));
    VK_CHECK_LOG_THROW(vkCreateBuffer(pddit->second.device, &bufferCreateInfo, nullptr, &pddit->second.data[activeIndex].buffer), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(pddit->second.device, pddit->second.data[activeIndex].buffer, &memReqs);
    pddit->second.data[activeIndex].memoryBlock = allocator->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(pddit->second.data[activeIndex].memoryBlock.alignedSize == 0, "Cannot create a buffer " << usage);
    allocator->bindBufferMemory(renderContext.device, pddit->second.data[activeIndex].buffer, pddit->second.data[activeIndex].memoryBlock.alignedOffset);

    invalidateCommandBuffers();
  }
  if ( uglyGetSize(*sData) > 0)
  {
    if (memoryIsLocal)
    {
      std::shared_ptr<StagingBuffer> stagingBuffer = renderContext.device->acquireStagingBuffer( uglyGetPointer(*sData), uglyGetSize(*sData));
      auto staggingCommandBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
      VkBufferCopy copyRegion{};
      copyRegion.size = uglyGetSize(*sData);
      staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, pddit->second.data[activeIndex].buffer, copyRegion);
      renderContext.device->endSingleTimeCommands(staggingCommandBuffer, renderContext.queue);
      renderContext.device->releaseStagingBuffer(stagingBuffer);
    }
    else
    {
      allocator->copyToDeviceMemory(renderContext.device, pddit->second.data[activeIndex].memoryBlock.alignedOffset, uglyGetPointer(*sData), uglyGetSize(*sData), 0);
    }
  }
  invalidateDescriptors();
  pddit->second.valid[activeIndex] = true;
}

template <typename T>
void GenericBuffer<T>::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
    pdd.second.invalidate();
}

template <typename T>
DescriptorSetValue GenericBuffer<T>::getDescriptorSetValue(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(getKey(renderContext, perObjectBehaviour));
  CHECK_LOG_THROW(pddit == end(perObjectData), "GenericBuffer<T>::getDescriptorSetValue() : storage buffer was not validated");
  std::shared_ptr<T> sData = (perObjectBehaviour == pbPerDevice) ? data : pddit->second.commonData;
  return DescriptorSetValue(pddit->second.data[renderContext.activeIndex % activeCount].buffer, 0, uglyGetSize(*sData));
}

template <typename T>
VkBuffer GenericBuffer<T>::getHandleBuffer(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(getKey(renderContext, perObjectBehaviour));
  if (pddit == end(perObjectData))
    return VK_NULL_HANDLE;
  return pddit->second.data[renderContext.activeIndex % activeCount].buffer;
}

}
