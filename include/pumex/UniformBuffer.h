#pragma once
#include <memory>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/utils/Buffer.h>
#include <pumex/Device.h>
#include <pumex/Pipeline.h>

// Simple uniform buffer for handling one C++ struct
// One serious drawback : each uniform buffer allocates its own GPU memory.
// Keep this in mind if you plan to use many such uniform buffers

namespace pumex
{

template <typename T>
class UniformBuffer : public DescriptorSetSource
{
public:
  explicit UniformBuffer() = default;
  explicit UniformBuffer(const T& data);
  UniformBuffer(const UniformBuffer&) = delete;
  UniformBuffer& operator=(const UniformBuffer&) = delete;
  ~UniformBuffer();

  inline void set( const T& data );
  inline T    get() const;
  void        getDescriptorSetValues(VkDevice device, std::vector<DescriptorSetValue>& values) const override;
  void        setDirty();
  void        validate(std::shared_ptr<pumex::Device> device);

private:
  struct PerDeviceData
  {
    PerDeviceData()
    {
    }

    bool                   dirty     = true;
    VkBuffer               uboBuffer = VK_NULL_HANDLE;
    VkDeviceMemory         uboMemory = VK_NULL_HANDLE;
  };
  T uboData;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

template <typename T>
UniformBuffer<T>::UniformBuffer(const T& data)
  : uboData(data)
{
}

template <typename T>
UniformBuffer<T>::~UniformBuffer()
{
  for (auto& pdd : perDeviceData)
    destroyBuffer(pdd.first, pdd.second.uboBuffer, pdd.second.uboMemory);
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
void UniformBuffer<T>::getDescriptorSetValues(VkDevice device, std::vector<DescriptorSetValue>& values) const
{
  auto pddit = perDeviceData.find(device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "UniformBuffer<T>::getDescriptorBufferInfo : uniform buffer was not validated");

  values.push_back( DescriptorSetValue(pddit->second.uboBuffer, 0, sizeof(T)) );
}

template <typename T>
void UniformBuffer<T>::setDirty()
{
  for (auto& pdd : perDeviceData)
    pdd.second.dirty = true;
}

template <typename T>
void UniformBuffer<T>::validate(std::shared_ptr<pumex::Device> device)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (!pddit->second.dirty)
    return;
  if (pddit->second.uboBuffer == VK_NULL_HANDLE)
  {
    VkDeviceSize uboSize = createBuffer(device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(T), &pddit->second.uboBuffer, &pddit->second.uboMemory);
    CHECK_LOG_THROW(uboSize == 0, "Cannot create UBO");
    notifyDescriptorSets();
  }
  uint8_t *pData;
  VK_CHECK_LOG_THROW(vkMapMemory(device->device, pddit->second.uboMemory, 0, sizeof(T), 0, (void **)&pData), "Cannot map memory");
  memcpy(pData, &uboData, sizeof(T));
  vkUnmapMemory(device->device, pddit->second.uboMemory);
  pddit->second.dirty = false;
}

}