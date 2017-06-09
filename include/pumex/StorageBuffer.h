#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Device.h>
#include <pumex/Pipeline.h>

// Simple storage buffer for handling a vector of C++ structs

namespace pumex
{

template <typename T>
class StorageBuffer : public DescriptorSetSource
{
public:
  explicit StorageBuffer(std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0 );
  explicit StorageBuffer(const T& data, std::weak_ptr<DeviceMemoryAllocator> allocator, uint32_t activeCount = 1, VkBufferUsageFlagBits additionalFlags = (VkBufferUsageFlagBits)0);
  StorageBuffer(const StorageBuffer&)            = delete;
  StorageBuffer& operator=(const StorageBuffer&) = delete;
  ~StorageBuffer();

  inline void                    set(const std::vector<T>& data);
  inline const std::vector<T>&   get() const;
  void                           getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const override;
  void                           setDirty();
  void                           validate(std::shared_ptr<Device> device);

  inline void setActiveIndex(uint32_t index);
  inline uint32_t getActiveIndex() const;

private:
  struct PerDeviceData
  {
    PerDeviceData(uint32_t ac)
    {
      dirty.resize(ac, true);
      storageBuffer.resize(ac, VK_NULL_HANDLE);
      memoryBlock.resize(ac, DeviceMemoryBlock());
    }

    std::vector<bool>               dirty;
    std::vector<VkBuffer>           storageBuffer;
    std::vector<DeviceMemoryBlock>  memoryBlock;
  };
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
  std::vector<T>                              storageData;
  std::weak_ptr<DeviceMemoryAllocator>        allocator;
  VkBufferUsageFlagBits                       additionalFlags;
  uint32_t                                    activeCount;
  uint32_t                                    activeIndex = 0;

};

template <typename T>
StorageBuffer<T>::StorageBuffer(std::weak_ptr<DeviceMemoryAllocator> a, uint32_t ac, VkBufferUsageFlagBits af)
  : allocator{ a }, additionalFlags{ af }, activeCount{ ac }
{
}

template <typename T>
StorageBuffer<T>::StorageBuffer(const T& data, std::weak_ptr<DeviceMemoryAllocator> a, uint32_t ac, VkBufferUsageFlagBits af)
  : storageData(data), allocator{ a }, additionalFlags{ af }, activeCount{ ac }
{
}

template <typename T>
StorageBuffer<T>::~StorageBuffer()
{
  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  for (auto& pdd : perDeviceData)
  {
    for (uint32_t i = 0; i < activeCount; ++i)
    {
      vkDestroyBuffer(pdd.first, pdd.second.storageBuffer[i], nullptr);
      alloc->deallocate(pdd.first, pdd.second.memoryBlock[i]);
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
  setDirty();
}

template <typename T>
const std::vector<T>& StorageBuffer<T>::get() const
{
  return storageData;
}

template <typename T>
void StorageBuffer<T>::getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const
{
  auto pddit = perDeviceData.find(device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "StorageBuffer<T>::getDescriptorBufferInfo : storage buffer was not validated");

  values.push_back(DescriptorSetValue(pddit->second.storageBuffer[index % activeCount], 0, sizeof(T) * storageData.size()));
}

template <typename T>
void StorageBuffer<T>::setDirty()
{
  for (auto& pdd : perDeviceData)
    for(uint32_t i=0; i<activeCount; ++i)
      pdd.second.dirty[i] = true;
}

template <typename T>
void StorageBuffer<T>::validate(std::shared_ptr<Device> device)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData(activeCount) }).first;
  if (!pddit->second.dirty[activeIndex])
    return;
  std::shared_ptr<DeviceMemoryAllocator> alloc = allocator.lock();
  if (pddit->second.storageBuffer[activeIndex] != VK_NULL_HANDLE  && pddit->second.memoryBlock[activeIndex].size < sizeof(T)*storageData.size())
  {
    vkDestroyBuffer(pddit->first, pddit->second.storageBuffer[activeIndex], nullptr);
    alloc->deallocate(pddit->first, pddit->second.memoryBlock[activeIndex]);
    pddit->second.storageBuffer[activeIndex] = VK_NULL_HANDLE;
    pddit->second.memoryBlock[activeIndex] = DeviceMemoryBlock();
  }

  if (pddit->second.storageBuffer[activeIndex] == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
      bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | additionalFlags;
      bufferCreateInfo.size  = std::max<VkDeviceSize>(1, sizeof(T)*storageData.size());
    VK_CHECK_LOG_THROW(vkCreateBuffer(device->device, &bufferCreateInfo, nullptr, &pddit->second.storageBuffer[activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->device, pddit->second.storageBuffer[activeIndex], &memReqs);
    pddit->second.memoryBlock[activeIndex] = alloc->allocate(device, memReqs);
    CHECK_LOG_THROW(pddit->second.memoryBlock[activeIndex].size == 0, "Cannot create SBO");
    VK_CHECK_LOG_THROW(vkBindBufferMemory(pddit->first, pddit->second.storageBuffer[activeIndex], pddit->second.memoryBlock[activeIndex].memory, pddit->second.memoryBlock[activeIndex].memoryOffset), "Cannot bind memory to buffer");

    notifyDescriptorSets();
  }
  if (storageData.size() > 0)
  {
    uint8_t *pData;
    VK_CHECK_LOG_THROW(vkMapMemory(device->device, pddit->second.memoryBlock[activeIndex].memory, pddit->second.memoryBlock[activeIndex].memoryOffset, sizeof(T)*storageData.size(), 0, (void **)&pData), "Cannot map memory");
    memcpy(pData, storageData.data(), sizeof(T)*storageData.size());
    vkUnmapMemory(device->device, pddit->second.memoryBlock[activeIndex].memory);
  }
  pddit->second.dirty[activeIndex] = false;
}

template <typename T>
void StorageBuffer<T>::setActiveIndex(uint32_t index) { activeIndex = index % activeCount; }
template <typename T>
uint32_t StorageBuffer<T>::getActiveIndex() const { return activeIndex; }


}
