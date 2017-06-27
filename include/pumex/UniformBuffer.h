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
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/Pipeline.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>


// Simple uniform buffer that stores THE SAME C++ struct on EACH of the Vulkan devices

namespace pumex
{

template <typename T>
class UniformBuffer : public DescriptorSetSource
{
public:
  UniformBuffer()                                = delete;
  explicit UniformBuffer(std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0);
  explicit UniformBuffer(const T& data, std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0);
  UniformBuffer(const UniformBuffer&)            = delete;
  UniformBuffer& operator=(const UniformBuffer&) = delete;
  ~UniformBuffer();

  inline void set( const T& data );
  inline T    get() const;
  void        getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const override;
  void        setDirty();
  void        validate(Device* device, CommandPool* commandPool, VkQueue queue);

  inline void setActiveIndex(uint32_t index);
  inline uint32_t getActiveIndex() const;

private:
  struct PerDeviceData
  {
    PerDeviceData(uint32_t ac)
    {
      dirty.resize(ac, true);
      uboBuffer.resize(ac, VK_NULL_HANDLE);
      memoryBlock.resize(ac, DeviceMemoryBlock());
    }

    std::vector<bool>               dirty;
    std::vector<VkBuffer>           uboBuffer;
    std::vector<DeviceMemoryBlock>  memoryBlock;
  };
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
  T                                           uboData;
  std::weak_ptr<DeviceMemoryAllocator>        allocator;
  VkBufferUsageFlagBits                       additionalFlags;
  uint32_t                                    activeCount;
  uint32_t                                    activeIndex = 0;

};

template <typename T>
UniformBuffer<T>::UniformBuffer(std::weak_ptr<DeviceMemoryAllocator> a, uint32_t ac, VkBufferUsageFlagBits af)
  : uboData(), allocator{ a }, additionalFlags{ af }, activeCount{ ac }
{
}

template <typename T>
UniformBuffer<T>::UniformBuffer(const T& data, std::weak_ptr<DeviceMemoryAllocator> a, uint32_t ac, VkBufferUsageFlagBits af)
  : uboData(data), allocator{ a }, additionalFlags{ af }, activeCount{ ac }
{
}

template <typename T>
UniformBuffer<T>::~UniformBuffer()
{
  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  for (auto& pdd : perDeviceData)
  {
    for (uint32_t i = 0; i < activeCount; ++i)
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
  setDirty();
}

template <typename T>
T UniformBuffer<T>::get() const
{
  return uboData;
}

template <typename T>
void UniformBuffer<T>::getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const
{
  auto pddit = perDeviceData.find(device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "UniformBuffer<T>::getDescriptorBufferInfo : uniform buffer was not validated");

  values.push_back( DescriptorSetValue(pddit->second.uboBuffer[index % activeCount], 0, sizeof(T)));
}

template <typename T>
void UniformBuffer<T>::setDirty()
{
  for (auto& pdd : perDeviceData)
    for (uint32_t i = 0; i<activeCount; ++i)
      pdd.second.dirty[i] = true;
}

template <typename T>
void UniformBuffer<T>::validate(Device* device, CommandPool* commandPool, VkQueue queue)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData(activeCount) }).first;
  if (!pddit->second.dirty[activeIndex])
    return;

  bool memoryIsLocal = ((allocator.lock()->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (pddit->second.uboBuffer[activeIndex] == VK_NULL_HANDLE)
  {
    std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
    VkBufferCreateInfo bufferCreateInfo{};
      bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | additionalFlags | (memoryIsLocal ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
      bufferCreateInfo.size  = std::max<VkDeviceSize>(1, sizeof(T));
    VK_CHECK_LOG_THROW(vkCreateBuffer(device->device, &bufferCreateInfo, nullptr, &pddit->second.uboBuffer[activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->device, pddit->second.uboBuffer[activeIndex], &memReqs);
    pddit->second.memoryBlock[activeIndex] = alloc->allocate(device, memReqs);
    CHECK_LOG_THROW(pddit->second.memoryBlock[activeIndex].alignedSize == 0, "Cannot create UBO");
    VK_CHECK_LOG_THROW(vkBindBufferMemory(pddit->first, pddit->second.uboBuffer[activeIndex], pddit->second.memoryBlock[activeIndex].memory, pddit->second.memoryBlock[activeIndex].alignedOffset), "Cannot bind memory to buffer");

    notifyDescriptorSets();
  }
  if (memoryIsLocal)
  {
    std::shared_ptr<StagingBuffer> stagingBuffer = device->acquireStagingBuffer(&uboData, sizeof(T));
    auto staggingCommandBuffer = device->beginSingleTimeCommands(commandPool);
    VkBufferCopy copyRegion{};
    copyRegion.size = sizeof(T);
    staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, pddit->second.uboBuffer[activeIndex], copyRegion);
    device->endSingleTimeCommands(staggingCommandBuffer, queue);
    device->releaseStagingBuffer(stagingBuffer);
  }
  else
  {
    uint8_t *pData;
    VK_CHECK_LOG_THROW(vkMapMemory(device->device, pddit->second.memoryBlock[activeIndex].memory, pddit->second.memoryBlock[activeIndex].alignedOffset, sizeof(T), 0, (void **)&pData), "Cannot map memory");
    memcpy(pData, &uboData, sizeof(T));
    vkUnmapMemory(device->device, pddit->second.memoryBlock[activeIndex].memory);
  }
  pddit->second.dirty[activeIndex] = false;
}

template <typename T>
void UniformBuffer<T>::setActiveIndex(uint32_t index) { activeIndex = index % activeCount; }
template <typename T>
uint32_t UniformBuffer<T>::getActiveIndex() const { return activeIndex; }


}