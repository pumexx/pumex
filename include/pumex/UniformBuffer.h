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


// Simple uniform buffer that stores THE SAME C++ struct on EACH of the Vulkan devices

namespace pumex
{

template <typename T>
class UniformBuffer : public Resource
{
public:
  UniformBuffer()                                = delete;
  explicit UniformBuffer(std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlags additionalFlags = 0, PerObjectBehaviour perObjectBehaviour = pbPerDevice, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage);
  explicit UniformBuffer(const T& data, std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlags additionalFlags = 0, PerObjectBehaviour perObjectBehaviour = pbPerDevice, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage);
  UniformBuffer(const UniformBuffer&)            = delete;
  UniformBuffer& operator=(const UniformBuffer&) = delete;
  virtual ~UniformBuffer();

  inline void                       set( const T& data );
  inline void                       set(Surface* surface, const T& data);
  inline T                          get() const;
  inline T                          get(Surface* surface) const;

  std::pair<bool, VkDescriptorType> getDefaultDescriptorType() override;
  void                              validate(const RenderContext& renderContext) override;
  void                              invalidate() override;
  DescriptorSetValue                getDescriptorSetValue(const RenderContext& renderContext) override;

  VkBuffer                          getHandleBuffer(const RenderContext& renderContext);

private:
  template<typename X> 
  struct UniformBufferInternal
  {
    UniformBufferInternal()
      : uboBuffer(VK_NULL_HANDLE), memoryBlock(), uboData()
    {}
    VkBuffer           uboBuffer;
    DeviceMemoryBlock  memoryBlock;
    X                  uboData;
  };

  std::unordered_map<void*, PerObjectData<UniformBufferInternal<T>>> perObjectData;
  T                                                                  uboData;
  std::shared_ptr<DeviceMemoryAllocator>                             allocator;
  VkBufferUsageFlags                                                 additionalFlags;
};

template <typename T>
UniformBuffer<T>::UniformBuffer(std::shared_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlags af, PerObjectBehaviour pob, SwapChainImageBehaviour scib)
  : Resource{ pob, scib }, uboData(), allocator{ a }, additionalFlags{ af }
{
}

template <typename T>
UniformBuffer<T>::UniformBuffer(const T& data, std::shared_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlags af, PerObjectBehaviour pob, SwapChainImageBehaviour scib)
  : Resource{ pob, scib }, uboData(data), allocator{ a }, additionalFlags{ af }
{
}

template <typename T>
UniformBuffer<T>::~UniformBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
  {
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
    {
      vkDestroyBuffer(pdd.second.device, pdd.second.data[i].uboBuffer, nullptr);
      allocator->deallocate(pdd.second.device, pdd.second.data[i].memoryBlock);
    }
  }
}


template <typename T>
void UniformBuffer<T>::set(const T& data)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (perObjectBehaviour == pbPerDevice)
    uboData = data;
  else
  {
    for (auto& pdd : perObjectData)
      pdd.second.data[0].uboData = data;
  }
  invalidate();
}

template <typename T>
void UniformBuffer<T>::set(Surface* surface, const T& data)
{
  CHECK_LOG_THROW(perObjectBehaviour != pbPerSurface, "Cannot set data per surface for this uniform buffer");
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find((void*)(surface->surface));
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ (void*)(surface->surface), PerObjectData<UniformBufferInternal<T>>(surface->device.lock()->device, surface->surface, activeCount) }).first;
  pddit->second.data[0].uboData = data;
  pddit->second.invalidate();
}

template <typename T>
T UniformBuffer<T>::get() const
{
  std::lock_guard<std::mutex> lock(mutex);
  if (perObjectBehaviour == pbPerSurface)
  {
    auto pddit = begin(perObjectData);
    if (pddit == end(perObjectData))
      return T();
    return pddit->second.data[0].uboData;
  }
  return uboData;
}

template <typename T>
T UniformBuffer<T>::get(Surface* surface) const
{
  CHECK_LOG_THROW(perObjectBehaviour != pbPerSurface, "Cannot get data per surface for this uniform buffer");
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find((void*)(surface->surface));
  if (pddit == end(perObjectData))
    return T();
  return pddit->second.data[0].uboData;
}

template <typename T>
std::pair<bool, VkDescriptorType> UniformBuffer<T>::getDefaultDescriptorType()
{
  return{ true, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER };
}

template <typename T>
void UniformBuffer<T>::validate(const RenderContext& renderContext)
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
    pddit = perObjectData.insert({ keyValue, PerObjectData<UniformBufferInternal<T>>(renderContext) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  bool memoryIsLocal = ((allocator->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (pddit->second.data[activeIndex].uboBuffer == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | additionalFlags | (memoryIsLocal ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
    bufferCreateInfo.size = std::max<VkDeviceSize>(1, sizeof(T));
    VK_CHECK_LOG_THROW(vkCreateBuffer(pddit->second.device, &bufferCreateInfo, nullptr, &pddit->second.data[activeIndex].uboBuffer), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(pddit->second.device, pddit->second.data[activeIndex].uboBuffer, &memReqs);
    pddit->second.data[activeIndex].memoryBlock = allocator->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(pddit->second.data[activeIndex].memoryBlock.alignedSize == 0, "Cannot create UBO");
    allocator->bindBufferMemory(renderContext.device, pddit->second.data[activeIndex].uboBuffer, pddit->second.data[activeIndex].memoryBlock.alignedOffset);
    invalidateCommandBuffers();
  }
  if (memoryIsLocal)
  {
    std::shared_ptr<StagingBuffer> stagingBuffer;
    if(perObjectBehaviour == pbPerDevice)
      stagingBuffer = renderContext.device->acquireStagingBuffer(&uboData, sizeof(T));
    else
      stagingBuffer = renderContext.device->acquireStagingBuffer(&pddit->second.data[0].uboData, sizeof(T));
    auto staggingCommandBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
    VkBufferCopy copyRegion{};
    copyRegion.size = sizeof(T);
    staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, pddit->second.data[activeIndex].uboBuffer, copyRegion);
    renderContext.device->endSingleTimeCommands(staggingCommandBuffer, renderContext.queue);
    renderContext.device->releaseStagingBuffer(stagingBuffer);
  }
  else
  {
    if (perObjectBehaviour == pbPerDevice)
      allocator->copyToDeviceMemory(renderContext.device, pddit->second.data[activeIndex].memoryBlock.alignedOffset, &uboData, sizeof(T), 0);
    else
      allocator->copyToDeviceMemory(renderContext.device, pddit->second.data[activeIndex].memoryBlock.alignedOffset, &pddit->second.data[0].uboData, sizeof(T), 0);
  }
  pddit->second.valid[activeIndex] = true;
}

template <typename T>
void UniformBuffer<T>::invalidate()
{
  for (auto& pdd : perObjectData)
    pdd.second.invalidate();
  invalidateDescriptors();
}

template <typename T>
DescriptorSetValue UniformBuffer<T>::getDescriptorSetValue(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(getKey(renderContext,perObjectBehaviour));
  CHECK_LOG_THROW(pddit == end(perObjectData), "UniformBuffer<T>::getDescriptorSetValue() : uniform buffer was not validated");
  return DescriptorSetValue(pddit->second.data[renderContext.activeIndex % activeCount].uboBuffer, 0, sizeof(T));
}

template <typename T>
VkBuffer UniformBuffer<T>::getHandleBuffer(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(getKey(renderContext, perObjectBehaviour));
  if (pddit == end(perObjectData))
    return VK_NULL_HANDLE;
  return pddit->second.data[renderContext.activeIndex % activeCount].uboBuffer;
}


}