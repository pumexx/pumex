#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/utils/Buffer.h>
#include <pumex/Device.h>
#include <pumex/Pipeline.h>

// Simple storage buffer for handling a vector of C++ structs
// One serious drawback : each storage buffer allocates its own GPU memory.
// Keep this in mind if you plan to use many such buffers
// Things to consider in the future : circular buffers, common buffer allocation, 

namespace pumex
{

template <typename T>
class StorageBuffer : public DescriptorSetSource
{
public:
  explicit StorageBuffer(VkBufferUsageFlagBits additionalFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  explicit StorageBuffer(const T& data, VkBufferUsageFlagBits additionalFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  StorageBuffer(const StorageBuffer&)            = delete;
  StorageBuffer& operator=(const StorageBuffer&) = delete;
  ~StorageBuffer();

  inline void                    set(const std::vector<T>& data);
  inline const std::vector<T>&   get() const;
  void                           getDescriptorSetValues(VkDevice device, std::vector<DescriptorSetValue>& values) const override;
  void                           setDirty();
  void                           validate(std::shared_ptr<pumex::Device> device);

private:
  struct PerDeviceData
  {
    PerDeviceData()
    {
    }

    bool                   dirty         = true;
    VkDeviceSize           memorySize    = 0;
    VkBuffer               storageBuffer = VK_NULL_HANDLE;
    VkDeviceMemory         storageMemory = VK_NULL_HANDLE;
  };
  std::vector<T> storageData;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
  VkBufferUsageFlagBits additionalFlags;
};

template <typename T>
StorageBuffer<T>::StorageBuffer(VkBufferUsageFlagBits af)
  : additionalFlags{ af }
{
}

template <typename T>
StorageBuffer<T>::StorageBuffer(const T& data, VkBufferUsageFlagBits af)
  : storageData(data), additionalFlags{af}
{
}

template <typename T>
StorageBuffer<T>::~StorageBuffer()
{
  for (auto& pdd : perDeviceData)
    destroyBuffer(pdd.first, pdd.second.storageBuffer, pdd.second.storageMemory);
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
void StorageBuffer<T>::getDescriptorSetValues(VkDevice device, std::vector<DescriptorSetValue>& values) const
{
  auto pddit = perDeviceData.find(device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "StorageBuffer<T>::getDescriptorBufferInfo : storage buffer was not validated");

  values.push_back( DescriptorSetValue(pddit->second.storageBuffer, 0, sizeof(T) * storageData.size()) );
}

template <typename T>
void StorageBuffer<T>::setDirty()
{
  for (auto& pdd : perDeviceData)
    pdd.second.dirty = true;
}

template <typename T>
void StorageBuffer<T>::validate(std::shared_ptr<pumex::Device> device)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (!pddit->second.dirty)
    return;
  if (pddit->second.storageBuffer != VK_NULL_HANDLE  && pddit->second.memorySize < sizeof(T)*storageData.size())
  {
    destroyBuffer(pddit->first, pddit->second.storageBuffer, pddit->second.storageMemory);
    pddit->second.memorySize    = 0;
    pddit->second.storageBuffer = VK_NULL_HANDLE;
    pddit->second.storageMemory = VK_NULL_HANDLE;
  }

  if (pddit->second.storageBuffer == VK_NULL_HANDLE)
  {
    pddit->second.memorySize = createBuffer(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | additionalFlags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(T)*storageData.size(), &pddit->second.storageBuffer, &pddit->second.storageMemory);
    CHECK_LOG_THROW(pddit->second.memorySize == 0, "Cannot create SBO");
    notifyDescriptorSets();
  }
  if (storageData.size() > 0)
  {
    uint8_t *pData;
    VK_CHECK_LOG_THROW(vkMapMemory(device->device, pddit->second.storageMemory, 0, sizeof(T)*storageData.size(), 0, (void **)&pData), "Cannot map memory");
    memcpy(pData, storageData.data(), sizeof(T)*storageData.size());
    vkUnmapMemory(device->device, pddit->second.storageMemory);
  }
  pddit->second.dirty = false;
}

}
