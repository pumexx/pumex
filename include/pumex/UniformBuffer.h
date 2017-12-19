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
#include <unordered_map>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/Pipeline.h>
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
  explicit UniformBuffer(std::weak_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0);
  explicit UniformBuffer(const T& data, std::weak_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0);
  UniformBuffer(const UniformBuffer&)            = delete;
  UniformBuffer& operator=(const UniformBuffer&) = delete;
  ~UniformBuffer();

  inline void                       set( const T& data );
  inline T                          get() const;

  std::pair<bool, VkDescriptorType> getDefaultDescriptorType() override;
  void                              validate(const RenderContext& renderContext) override;
  void                              invalidate() override;
  void                              getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const override;

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
      uboBuffer.resize(ac, VK_NULL_HANDLE);
      memoryBlock.resize(ac, DeviceMemoryBlock());
    }

    std::vector<bool>               valid;
    std::vector<VkBuffer>           uboBuffer;
    std::vector<DeviceMemoryBlock>  memoryBlock;
  };

  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
  T                                           uboData;
  std::weak_ptr<DeviceMemoryAllocator>        allocator;
  VkBufferUsageFlagBits                       additionalFlags;
  uint32_t                                    activeCount = 1;

};

template <typename T>
UniformBuffer<T>::UniformBuffer(std::weak_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlagBits af)
  : uboData(), allocator{ a }, additionalFlags{ af }
{
}

template <typename T>
UniformBuffer<T>::UniformBuffer(const T& data, std::weak_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlagBits af)
  : uboData(data), allocator{ a }, additionalFlags{ af }
{
}

template <typename T>
UniformBuffer<T>::~UniformBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  for (auto& pdd : perDeviceData)
  {
    for (uint32_t i = 0; i < pdd.second.uboBuffer.size(); ++i)
    {
      vkDestroyBuffer(pdd.first, pdd.second.uboBuffer[i], nullptr);
      alloc->deallocate(pdd.first, pdd.second.memoryBlock[i]);
    }
  }
}


template <typename T>
void UniformBuffer<T>::set(const T& data)
{
  uboData = data;
  invalidate();
}

template <typename T>
T UniformBuffer<T>::get() const
{
  return uboData;
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
  if (renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perDeviceData)
      pdd.second.resize(activeCount);
  }
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ renderContext.vkDevice, PerDeviceData(activeCount) }).first;
  if (pddit->second.valid[renderContext.activeIndex])
    return;

  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  bool memoryIsLocal = ((alloc->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (pddit->second.uboBuffer[renderContext.activeIndex] == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | additionalFlags | (memoryIsLocal ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
    bufferCreateInfo.size = std::max<VkDeviceSize>(1, sizeof(T));
    VK_CHECK_LOG_THROW(vkCreateBuffer(renderContext.vkDevice, &bufferCreateInfo, nullptr, &pddit->second.uboBuffer[renderContext.activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(renderContext.vkDevice, pddit->second.uboBuffer[renderContext.activeIndex], &memReqs);
    pddit->second.memoryBlock[renderContext.activeIndex] = alloc->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(pddit->second.memoryBlock[renderContext.activeIndex].alignedSize == 0, "Cannot create UBO");
    alloc->bindBufferMemory(renderContext.device, pddit->second.uboBuffer[renderContext.activeIndex], pddit->second.memoryBlock[renderContext.activeIndex].alignedOffset);
    invalidateCommandBuffers();
  }
  if (memoryIsLocal)
  {
    std::shared_ptr<StagingBuffer> stagingBuffer = renderContext.device->acquireStagingBuffer(&uboData, sizeof(T));
    auto staggingCommandBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
    VkBufferCopy copyRegion{};
    copyRegion.size = sizeof(T);
    staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, pddit->second.uboBuffer[renderContext.activeIndex], copyRegion);
    renderContext.device->endSingleTimeCommands(staggingCommandBuffer, renderContext.presentationQueue);
    renderContext.device->releaseStagingBuffer(stagingBuffer);
  }
  else
  {
    alloc->copyToDeviceMemory(renderContext.device, pddit->second.memoryBlock[renderContext.activeIndex].alignedOffset, &uboData, sizeof(T), 0);
  }
  pddit->second.valid[renderContext.activeIndex] = true;
}

template <typename T>
void UniformBuffer<T>::getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "UniformBuffer<T>::getDescriptorBufferInfo : uniform buffer was not validated");

  values.push_back( DescriptorSetValue(pddit->second.uboBuffer[renderContext.activeIndex], 0, sizeof(T)));
}

template <typename T>
void UniformBuffer<T>::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    for (uint32_t i = 0; i<pdd.second.valid.size(); ++i)
      pdd.second.valid[i] = false;
  invalidateDescriptors();
}

}