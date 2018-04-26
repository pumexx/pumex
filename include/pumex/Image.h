//
// Copyright(c) 2017-2018 Paweł Księżopolski ( pumexx )
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
#include <vulkan/vulkan.h>
#include <gli/texture.hpp>
#include <pumex/Export.h>
#include <pumex/DeviceMemoryAllocator.h>

namespace pumex
{

// struct representing all options required to create or describe VkImage
struct PUMEX_EXPORT ImageTraits
{
  explicit ImageTraits() = default;
  explicit ImageTraits(VkImageUsageFlags usage, VkFormat format, const VkExtent3D& extent, uint32_t mipLevels = 1, uint32_t arrayLayers = 1, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, 
    bool linearTiling = false, VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, VkMemoryPropertyFlags memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VkImageCreateFlags imageCreate = 0,
    VkImageType imageType = VK_IMAGE_TYPE_2D, VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE);
  
  VkImageUsageFlags        usage          = VK_IMAGE_USAGE_SAMPLED_BIT;
  VkFormat                 format         = VK_FORMAT_B8G8R8A8_UNORM;
  VkExtent3D               extent         = VkExtent3D{ 1, 1, 1 };
  uint32_t                 mipLevels      = 1;
  uint32_t                 arrayLayers    = 1;
  VkSampleCountFlagBits    samples        = VK_SAMPLE_COUNT_1_BIT;
  bool                     linearTiling   = false;
  VkImageLayout            initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageCreateFlags       imageCreate    = 0;
  VkImageType              imageType      = VK_IMAGE_TYPE_2D;
  VkSharingMode            sharingMode    = VK_SHARING_MODE_EXCLUSIVE;
  VkMemoryPropertyFlags    memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
};

// Class implementing Vulkan image and imageview
class PUMEX_EXPORT Image
{
public:
  Image()                            = delete;
  // user creates VkImage and assigns memory to it
  explicit Image(Device* device, const ImageTraits& imageTraits, std::shared_ptr<DeviceMemoryAllocator> allocator);
  // user delivers VkImage, Image does not own it, just creates VkImageView
  explicit Image(Device* device, VkImage image, VkFormat format, const VkExtent3D& extent, uint32_t mipLevels = 1, uint32_t arrayLayers = 1);
  Image(const Image&)                = delete;
  Image& operator=(const Image&)     = delete;
  virtual ~Image();

  inline VkImage            getHandleImage() const;
  inline VkDeviceSize       getMemorySize() const;
  inline const ImageTraits& getImageTraits() const;

  void                      getImageSubresourceLayout(VkImageSubresource& subRes, VkSubresourceLayout& subResLayout) const;
  void*                     mapMemory(size_t offset, size_t range, VkMemoryMapFlags flags=0);
  void                      unmapMemory();
protected:
  ImageTraits                            imageTraits;
  VkDevice                               device       = VK_NULL_HANDLE;
  std::shared_ptr<DeviceMemoryAllocator> allocator;
  VkImage                                image        = VK_NULL_HANDLE;
  DeviceMemoryBlock                      memoryBlock;
  bool                                   ownsImage    = true;

};

// inlines 
VkImage              Image::getHandleImage() const { return image; }
VkDeviceSize         Image::getMemorySize() const  { return memoryBlock.alignedSize; }
const ImageTraits&   Image::getImageTraits() const { return imageTraits; }


// helper functions
PUMEX_EXPORT VkFormat           vulkanFormatFromGliFormat(gli::texture::format_type format);
PUMEX_EXPORT VkImageViewType    vulkanViewTypeFromGliTarget(gli::texture::target_type target);
PUMEX_EXPORT VkImageType        vulkanImageTypeFromTextureExtents(const gli::extent3d& extents);
PUMEX_EXPORT VkComponentSwizzle vulkanSwizzlesFromGliSwizzles(const gli::swizzle& s);
PUMEX_EXPORT VkComponentMapping vulkanComponentMappingFromGliComponentMapping(const gli::swizzles& swz);

// Texture files are loaded through TextureLoader. Currently only gli library is used to load them
// This is temporary solution.
class PUMEX_EXPORT TextureLoader
{
public:
  virtual std::shared_ptr<gli::texture> load(const std::string& fileName) = 0;
};

}

