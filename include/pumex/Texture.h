#pragma once
#include <unordered_map>
#include <memory>
#include <pumex/Export.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <gli/texture.hpp>
#include <pumex/Pipeline.h>

namespace pumex
{

class Device;
class CommandPool;

struct PUMEX_EXPORT TextureTraits
{
  explicit TextureTraits() = default;

  VkImageUsageFlags       imageUsageFlags         = VK_IMAGE_USAGE_SAMPLED_BIT;
  bool                    linearTiling            = false; 
  VkFilter                magFilter               = VK_FILTER_LINEAR;
  VkFilter                minFilter               = VK_FILTER_LINEAR;
  VkSamplerMipmapMode     mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  VkSamplerAddressMode    addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode    addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode    addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  float                   mipLodBias              = 0.0f;
  VkBool32                anisotropyEnable        = VK_TRUE;
  float                   maxAnisotropy           = 8;
  VkBool32                compareEnable           = false;
  VkCompareOp             compareOp               = VK_COMPARE_OP_NEVER;
  VkBorderColor           borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  VkBool32                unnormalizedCoordinates = false;
};

// class implementing Vulkan image. 
// Uses gli::texture to hold texture on CPU
// Texture may contain usual textures, texture arrays, texture cubes, arrays of texture cubes etc, but cubes were not tested in real life ( be aware )
class PUMEX_EXPORT Texture : public DescriptorSetSource
{
public:
  explicit Texture();
  explicit Texture(const gli::texture& texture, const TextureTraits& traits);
  Texture(const Texture&)            = delete;
  Texture& operator=(const Texture&) = delete;

  ~Texture();

  void      setDirty();
  VkImage   getHandleImage(VkDevice device) const;
  VkSampler getHandleSampler(VkDevice device) const;
  void      validate(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandPool> commandPool, VkQueue queue);
  void      getDescriptorSetValues(VkDevice device, std::vector<DescriptorSetValue>& values) const override;

  void setLayer(uint32_t layer, const gli::texture& tex);

  std::shared_ptr<gli::texture> texture;
  TextureTraits                 traits;
private:
  VkCommandPool commandPool     = VK_NULL_HANDLE;
  VkQueue       queue           = VK_NULL_HANDLE;

  struct PerDeviceData
  {
    PerDeviceData()
    {
    }

    bool                  dirty        = true;
    VkImage               image        = VK_NULL_HANDLE;
    VkDeviceMemory        deviceMemory = VK_NULL_HANDLE;
    VkSampler             sampler      = VK_NULL_HANDLE;
    VkImageView           imageView    = VK_NULL_HANDLE;
    VkImageLayout         imageLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  };
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

// This is temporary solution.
class PUMEX_EXPORT TextureLoader
{
public:
  virtual std::shared_ptr<gli::texture> load(const std::string& fileName) = 0;
};
	
PUMEX_EXPORT void setImageLayout( VkCommandBuffer cmdbuffer, VkImage image, VkImageAspectFlags aspectMask, 
  VkImageLayout oldImageLayout, VkImageLayout newImageLayout,VkImageSubresourceRange subresourceRange);
  
PUMEX_EXPORT void setImageLayout( VkCommandBuffer cmdbuffer, VkImage image, VkImageAspectFlags aspectMask, 
		VkImageLayout oldImageLayout, VkImageLayout newImageLayout);
}

