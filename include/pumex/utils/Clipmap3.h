#pragma once
#include <pumex/Export.h>
#include <pumex/Pipeline.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Texture.h>
#include <pumex/Device.h>
#include <pumex/Command.h>

namespace pumex
{

class Device;
class CommandPool;

class PUMEX_EXPORT Clipmap3 : public DescriptorSetSource
{
public:
  Clipmap3()                           = delete;
  explicit Clipmap3( uint32_t textureQuantity, uint32_t textureSize, VkClearValue initValue, const ImageTraits& imageTraits, const TextureTraits& textureTraits, std::weak_ptr<DeviceMemoryAllocator> allocator);
  Clipmap3(const Clipmap3&)            = delete;
  Clipmap3& operator=(const Clipmap3&) = delete;

  virtual ~Clipmap3();

  Image*    getHandleImage(VkDevice device, uint32_t layer) const;
  void      validate(Device* device, CommandPool* commandPool, VkQueue queue);
  void      getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const override;

protected:
  uint32_t                             textureQuantity;
  uint32_t                             textureSize;
  VkClearValue                         initValue;
  ImageTraits                          imageTraits;
  TextureTraits                        textureTraits;
  std::weak_ptr<DeviceMemoryAllocator> allocator;
private:
  struct PerDeviceData
  {
    PerDeviceData()
    {
    }
    std::vector<std::shared_ptr<Image>> images;
    VkSampler                           sampler = VK_NULL_HANDLE;

  };
  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

}