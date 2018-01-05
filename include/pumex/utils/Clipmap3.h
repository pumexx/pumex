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

class PUMEX_EXPORT Clipmap3 : public Resource
{
public:
  Clipmap3()                           = delete;
  explicit Clipmap3( uint32_t textureQuantity, uint32_t textureSize, VkClearValue initValue, const ImageTraits& imageTraits, const SamplerTraits& textureTraits, std::shared_ptr<DeviceMemoryAllocator> allocator);
  Clipmap3(const Clipmap3&)            = delete;
  Clipmap3& operator=(const Clipmap3&) = delete;

  virtual ~Clipmap3();

  Image*    getHandleImage(VkDevice device, uint32_t layer) const;
  void      validate(Device* device, CommandPool* commandPool, VkQueue queue);
  void      getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const override;

protected:
  uint32_t                             textureQuantity;
  uint32_t                             textureSize;
  VkClearValue                         initValue;
  ImageTraits                          imageTraits;
  SamplerTraits                        textureTraits;
  std::shared_ptr<DeviceMemoryAllocator> allocator;
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

template <typename T>
glm::tmat4x4<T, glm::defaultp> orthoGL( T left, T right, T bottom, T top,	T zNear, T zFar	)
{
  glm::tmat4x4<T, glm::defaultp> Result(1);
  Result[0][0] = static_cast<T>(2) / (right - left);
  Result[1][1] = static_cast<T>(2) / (top - bottom);
  Result[3][0] = - (right + left) / (right - left);
  Result[3][1] = - (top + bottom) / (top - bottom);

  Result[2][2] = - static_cast<T>(2) / (zFar - zNear);
  Result[3][2] = - (zFar + zNear) / (zFar - zNear);

  return Result;
}


}