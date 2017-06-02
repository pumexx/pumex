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

struct PUMEX_EXPORT ImageTraits
{
  explicit ImageTraits() = default;
  explicit ImageTraits(VkImageUsageFlags usage, VkFormat format, const VkExtent3D& extent, bool linearTiling = false, uint32_t mipLevels = 1,
    uint32_t arrayLayers = 1, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, VkMemoryPropertyFlags memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VkImageCreateFlags imageCreate = 0,
    VkImageType imageType = VK_IMAGE_TYPE_2D, VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D, const gli::swizzles& swizzles = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA));
  
  VkImageUsageFlags        usage          = VK_IMAGE_USAGE_SAMPLED_BIT;
  bool                     linearTiling   = false;
  VkFormat                 format         = VK_FORMAT_B8G8R8A8_UNORM;
  VkExtent3D               extent         = VkExtent3D{ 1, 1, 1 };
  uint32_t                 mipLevels      = 1;
  uint32_t                 arrayLayers    = 1;
  VkSampleCountFlagBits    samples        = VK_SAMPLE_COUNT_1_BIT;
  VkImageLayout            initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageCreateFlags       imageCreate    = 0;
  VkImageType              imageType      = VK_IMAGE_TYPE_2D;
  VkSharingMode            sharingMode    = VK_SHARING_MODE_EXCLUSIVE;
  VkImageViewType          viewType       = VK_IMAGE_VIEW_TYPE_2D;
  gli::swizzles            swizzles       = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA);
  VkImageAspectFlags       aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  VkMemoryPropertyFlags    memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

};

struct PUMEX_EXPORT TextureTraits
{
  explicit TextureTraits() = default;
  explicit TextureTraits(VkImageUsageFlags usage, bool linearTiling = false, VkFilter magFilter = VK_FILTER_LINEAR, VkFilter minFilter = VK_FILTER_LINEAR, VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT, VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT, VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    float mipLodBias = 0.0f, VkBool32 anisotropyEnable = VK_TRUE, float maxAnisotropy = 8, VkBool32 compareEnable = false, VkCompareOp compareOp = VK_COMPARE_OP_NEVER, VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE, VkBool32 unnormalizedCoordinates = false);

  VkImageUsageFlags       usage                   = VK_IMAGE_USAGE_SAMPLED_BIT;
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

// Class implementing Vulkan image and imageview. Class exists per device
class PUMEX_EXPORT Image
{
public:
  Image()                            = delete;
  // user creates VkImage and assigns memory to it
  explicit Image(std::shared_ptr<Device> device, const ImageTraits& imageTraits ); 
  // user delivers VkImage, Image does not own it, just creates VkImageView
  explicit Image(std::shared_ptr<Device> device, VkImage image, VkFormat format, uint32_t mipLevels = 1, uint32_t arrayLayers = 1, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D, const gli::swizzles& swizzles = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA) );
  Image(const Image&)                = delete;
  Image& operator=(const Image&)     = delete;
  virtual ~Image();

  inline VkImage            getImage() const;
  inline VkImageView        getImageView() const;
  inline VkImageLayout      getImageLayout() const;
  inline VkMemoryRequirements getMemoryRequirements() const;
  inline const ImageTraits& getImageTraits() const;

  void  getImageSubresourceLayout(VkImageSubresource& subRes, VkSubresourceLayout& subResLayout) const;
  void* mapMemory(size_t offset, size_t range, VkMemoryMapFlags flags=0);
  void  unmapMemory();

  void setImageLayout(VkImageLayout newLayout);


protected:
  ImageTraits          imageTraits;
  VkDevice             device       = VK_NULL_HANDLE;
  VkImage              image        = VK_NULL_HANDLE;
  VkDeviceMemory       deviceMemory = VK_NULL_HANDLE;
  VkImageView          imageView    = VK_NULL_HANDLE;
  VkImageLayout        imageLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  VkMemoryRequirements memReqs;
  bool                 ownsImage    = true;

};


// Uses gli::texture to hold texture on CPU
// Texture may contain usual textures, texture arrays, texture cubes, arrays of texture cubes etc, but cubes were not tested in real life ( be aware )
class PUMEX_EXPORT Texture : public DescriptorSetSource
{
public:
  explicit Texture()                 = delete;
  explicit Texture(const gli::texture& texture, const TextureTraits& traits);
  Texture(const Texture&)            = delete;
  Texture& operator=(const Texture&) = delete;

  virtual ~Texture();

  void      setDirty();
  Image*    getHandleImage(VkDevice device) const;
  VkSampler getHandleSampler(VkDevice device) const;
  void      validate(std::shared_ptr<Device> device, std::shared_ptr<CommandPool> commandPool, VkQueue queue);
  void      getDescriptorSetValues(VkDevice device, std::vector<DescriptorSetValue>& values) const override;

  void setLayer(uint32_t layer, const gli::texture& tex);

  std::shared_ptr<gli::texture> texture;
  TextureTraits                 traits;
private:
//  VkCommandPool commandPool     = VK_NULL_HANDLE;
//  VkQueue       queue           = VK_NULL_HANDLE;

  struct PerDeviceData
  {
    PerDeviceData()
    {
    }

    bool                   dirty        = true;
    std::shared_ptr<Image> image;
    VkSampler              sampler      = VK_NULL_HANDLE;
  };
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

// This is temporary solution.
class PUMEX_EXPORT TextureLoader
{
public:
  virtual std::shared_ptr<gli::texture> load(const std::string& fileName) = 0;
};

// inlines 
VkImage              Image::getImage() const              { return image; }
VkImageView          Image::getImageView() const          { return imageView; }
VkImageLayout        Image::getImageLayout() const        { return imageLayout; }
VkMemoryRequirements Image::getMemoryRequirements() const { return memReqs; }
const ImageTraits&   Image::getImageTraits() const        { return imageTraits; }


// helper functions
PUMEX_EXPORT VkFormat vulkanFormatFromGliFormat(gli::texture::format_type format);
PUMEX_EXPORT VkImageViewType vulkanViewTypeFromGliTarget(gli::texture::target_type target);
PUMEX_EXPORT VkComponentSwizzle vulkanSwizzlesFromGliSwizzles(const gli::swizzle& s);
PUMEX_EXPORT VkComponentMapping vulkanComponentMappingFromGliComponentMapping(const gli::swizzles& swz);
	
}

