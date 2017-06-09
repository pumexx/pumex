#pragma once
#include <memory>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Device.h>
#include <pumex/Pipeline.h>

// Simple uniform buffer for handling one C++ struct

namespace pumex
{

template <typename T>
class UniformBuffer : public DescriptorSetSource
{
public:
  explicit UniformBuffer()                       = delete;
  explicit UniformBuffer(std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0);
  explicit UniformBuffer(const T& data, std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0);
  UniformBuffer(const UniformBuffer&)            = delete;
  UniformBuffer& operator=(const UniformBuffer&) = delete;
  ~UniformBuffer();

  inline void set( const T& data );
  inline T    get() const;
  void        getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const override;
  void        setDirty();
  void        validate(std::shared_ptr<Device> device);

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
void UniformBuffer<T>::validate(std::shared_ptr<Device> device)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData(activeCount) }).first;
  if (!pddit->second.dirty[activeIndex])
    return;
  if (pddit->second.uboBuffer[activeIndex] == VK_NULL_HANDLE)
  {
    std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
    VkBufferCreateInfo bufferCreateInfo{};
      bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | additionalFlags;
      bufferCreateInfo.size  = std::max<VkDeviceSize>(1, sizeof(T));
    VK_CHECK_LOG_THROW(vkCreateBuffer(device->device, &bufferCreateInfo, nullptr, &pddit->second.uboBuffer[activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->device, pddit->second.uboBuffer[activeIndex], &memReqs);
    pddit->second.memoryBlock[activeIndex] = alloc->allocate(device, memReqs);
    CHECK_LOG_THROW(pddit->second.memoryBlock[activeIndex].size == 0, "Cannot create UBO");
    VK_CHECK_LOG_THROW(vkBindBufferMemory(pddit->first, pddit->second.uboBuffer[activeIndex], pddit->second.memoryBlock[activeIndex].memory, pddit->second.memoryBlock[activeIndex].memoryOffset), "Cannot bind memory to buffer");

    notifyDescriptorSets();
  }
  uint8_t *pData;
  VK_CHECK_LOG_THROW(vkMapMemory(device->device, pddit->second.memoryBlock[activeIndex].memory, pddit->second.memoryBlock[activeIndex].memoryOffset, sizeof(T), 0, (void **)&pData), "Cannot map memory");
  memcpy(pData, &uboData, sizeof(T));
  vkUnmapMemory(device->device, pddit->second.memoryBlock[activeIndex].memory);

  pddit->second.dirty[activeIndex] = false;
}

}