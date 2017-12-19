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


// Uniform buffer that stores different C++ struct per surface

namespace pumex
{

template <typename T>
class UniformBufferPerSurface : public Resource
{
public:
  UniformBufferPerSurface()                                          = delete;
  explicit UniformBufferPerSurface(std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0);
  UniformBufferPerSurface(const UniformBufferPerSurface&)            = delete;
  UniformBufferPerSurface& operator=(const UniformBufferPerSurface&) = delete;
  ~UniformBufferPerSurface();

  inline void                       set(const T& data);
  inline void                       set(Surface* surface, const T& data);
  inline T                          get(Surface* surface) const;

  std::pair<bool, VkDescriptorType> getDefaultDescriptorType() override;
  void                              validate(const RenderContext& renderContext) override;
  void                              invalidate() override;
  void                              getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const override;

  VkBuffer                          getBufferHandle(Surface* surface);

  inline void                       setActiveIndex(uint32_t index);
  inline uint32_t                   getActiveIndex() const;

private:
  struct PerSurfaceData
  {
    PerSurfaceData(uint32_t ac, VkDevice d)
      : device{ d }
    {
      valid.resize(ac, false);
      uboBuffer.resize(ac, VK_NULL_HANDLE);
      memoryBlock.resize(ac, DeviceMemoryBlock());
    }
    void invalidate()
    {
      std::fill(valid.begin(), valid.end(), false);
    }

    T                               uboData;
    VkDevice                        device;
    std::vector<bool>               valid;
    std::vector<VkBuffer>           uboBuffer;
    std::vector<DeviceMemoryBlock>  memoryBlock;
  };

  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
  std::weak_ptr<DeviceMemoryAllocator>             allocator;
  VkBufferUsageFlagBits                            additionalFlags;
  uint32_t                                         activeCount;
  uint32_t                                         activeIndex = 0;

};

template <typename T>
UniformBufferPerSurface<T>::UniformBufferPerSurface(std::weak_ptr<DeviceMemoryAllocator> a, uint32_t ac, VkBufferUsageFlagBits af)
  : allocator{ a }, additionalFlags{ af }, activeCount{ ac }
{
}

template <typename T>
UniformBufferPerSurface<T>::~UniformBufferPerSurface()
{
  std::lock_guard<std::mutex> lock(mutex);
  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  for (auto& pdd : perSurfaceData)
  {
    for (uint32_t i = 0; i < activeCount; ++i)
    {
      vkDestroyBuffer(pdd.second.device, pdd.second.uboBuffer[i], nullptr);
      alloc->deallocate(pdd.second.device, pdd.second.memoryBlock[i]);
    }
  }
}

template <typename T>
void UniformBufferPerSurface<T>::set(const T& data)
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perSurfaceData)
  {
    pdd.second.uboData = data;
    pdd.second.invalidate();
  }
}

template <typename T>
void UniformBufferPerSurface<T>::set(Surface* surface, const T& data)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perSurfaceData.find(surface->surface);
  if (it == perSurfaceData.end())
    it = perSurfaceData.insert({ surface->surface, PerSurfaceData(activeCount, surface->device.lock()->device) }).first;
  it->second.uboData = data;
  it->second.invalidate();
}

template <typename T>
T UniformBufferPerSurface<T>::get(Surface* surface) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perSurfaceData.find(surface->surface);
  if (it == perSurfaceData.end())
    it = perSurfaceData.insert({ surface->surface, PerSurfaceData(activeCount, surface->device.lock()->device) }).first;
  return it->second.uboData;
}

template <typename T>
std::pair<bool, VkDescriptorType> UniformBufferPerSurface<T>::getDefaultDescriptorType()
{
  return{ true, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER };
}

template <typename T>
void UniformBufferPerSurface<T>::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex); 
  auto it = perSurfaceData.find(renderContext.vkSurface);
  if (it == perSurfaceData.end())
    it = perSurfaceData.insert({ renderContext.vkSurface, PerSurfaceData(activeCount, renderContext.vkDevice) }).first;
  if (it->second.valid[activeIndex])
    return;

  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  bool memoryIsLocal = ((alloc->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (it->second.uboBuffer[activeIndex] == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | additionalFlags | (memoryIsLocal ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
    bufferCreateInfo.size = std::max<VkDeviceSize>(1, sizeof(T));
    VK_CHECK_LOG_THROW(vkCreateBuffer(renderContext.vkDevice, &bufferCreateInfo, nullptr, &it->second.uboBuffer[activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(renderContext.vkDevice, it->second.uboBuffer[activeIndex], &memReqs);
    it->second.memoryBlock[activeIndex] = alloc->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(it->second.memoryBlock[activeIndex].alignedSize == 0, "Cannot create UBO");
    alloc->bindBufferMemory(renderContext.device, it->second.uboBuffer[activeIndex], it->second.memoryBlock[activeIndex].alignedOffset);

    invalidateCommandBuffers();
  }
  if (memoryIsLocal)
  {
    std::shared_ptr<StagingBuffer> stagingBuffer = renderContext.device->acquireStagingBuffer(&it->second.uboData, sizeof(T));
    auto staggingCommandBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
    VkBufferCopy copyRegion{};
    copyRegion.size = sizeof(T);
    staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, it->second.uboBuffer[activeIndex], copyRegion);
    renderContext.device->endSingleTimeCommands(staggingCommandBuffer, renderContext.presentationQueue);
    renderContext.device->releaseStagingBuffer(stagingBuffer);
  }
  else
  {
    alloc->copyToDeviceMemory(renderContext.device, it->second.memoryBlock[activeIndex].alignedOffset, &it->second.uboData, sizeof(T), 0);
  }
  it->second.valid[activeIndex] = true;
}


template <typename T>
void UniformBufferPerSurface<T>::getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(renderContext.vkSurface);
  CHECK_LOG_THROW(pddit == perSurfaceData.end(), "UniformBufferPerSurface<T>::getDescriptorBufferInfo : uniform buffer was not validated");

  values.push_back( DescriptorSetValue(pddit->second.uboBuffer[renderContext.activeIndex % activeCount], 0, sizeof(T)));
}

template <typename T>
void UniformBufferPerSurface<T>::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perSurfaceData)
    pdd.second.invalidate();
  invalidateDescriptors();
}

template <typename T>
VkBuffer UniformBufferPerSurface<T>::getBufferHandle(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perSurfaceData.find(surface->surface);
  if (it == perSurfaceData.end())
    return VK_NULL_HANDLE;
  return it->second.uboBuffer[activeIndex];
}

template <typename T>
void UniformBufferPerSurface<T>::setActiveIndex(uint32_t index) { activeIndex = index % activeCount; }
template <typename T>
uint32_t UniformBufferPerSurface<T>::getActiveIndex() const { return activeIndex; }


}