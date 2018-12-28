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

#include <pumex/Image.h>
#include <pumex/Device.h>
#include <pumex/utils/Log.h>

using namespace pumex;
namespace pumex
{
  VkExtent3D makeVkExtent3D(const ImageSize& iSize)
  {
    return VkExtent3D
    { 
      static_cast<uint32_t>(iSize.size.x), 
      static_cast<uint32_t>(iSize.size.y), 
      static_cast<uint32_t>(iSize.size.z) 
    };
  }

  VkExtent3D makeVkExtent3D(const ImageSize& iSize, const VkExtent3D& extent)
  {
    return VkExtent3D
    {
      static_cast<uint32_t>(iSize.size.x * extent.width),
      static_cast<uint32_t>(iSize.size.y * extent.height),
      static_cast<uint32_t>(iSize.size.z * extent.depth)
    };
  }

  VkExtent3D makeVkExtent3D(const ImageSize& iSize, const VkExtent2D& extent)
  {
    return VkExtent3D
    {
      static_cast<uint32_t>(iSize.size.x * extent.width),
      static_cast<uint32_t>(iSize.size.y * extent.height),
      1
    };
  }

  VkExtent2D makeVkExtent2D(const ImageSize& iSize)
  {
    return VkExtent2D
    {
      static_cast<uint32_t>(iSize.size.x),
      static_cast<uint32_t>(iSize.size.y)
    };
  }

  VkExtent2D makeVkExtent2D(const ImageSize& iSize, const VkExtent2D& extent)
  {
    return VkExtent2D
    {
      static_cast<uint32_t>(iSize.size.x * extent.width),
      static_cast<uint32_t>(iSize.size.y * extent.height)
    };
  }

  VkRect2D makeVkRect2D(int32_t x, int32_t y, uint32_t width, uint32_t height)
  {
    return VkRect2D{ VkOffset2D{ x, y }, VkExtent2D{ width, height } };
  }

  VkRect2D makeVkRect2D(const ImageSize& iSize)
  {
    return VkRect2D{ VkOffset2D{0,0}, makeVkExtent2D(iSize) };
  }

  VkRect2D makeVkRect2D(const ImageSize& iSize, const VkExtent2D& extent)
  {
    return VkRect2D{ VkOffset2D{ 0,0 }, makeVkExtent2D(iSize, extent) };
  }

  VkViewport makeVkViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
  {
    return VkViewport{x,y,width,height,minDepth,maxDepth};
  }

  VkSampleCountFlagBits makeSamples(uint32_t samples)
  {
    switch (samples)
    {
    case 1:
      return VK_SAMPLE_COUNT_1_BIT;
    case 2:
      return VK_SAMPLE_COUNT_2_BIT;
    case 4:
      return VK_SAMPLE_COUNT_4_BIT;
    case 8:
      return VK_SAMPLE_COUNT_8_BIT;
    case 16:
      return VK_SAMPLE_COUNT_16_BIT;
    case 32:
      return VK_SAMPLE_COUNT_32_BIT;
    case 64:
      return VK_SAMPLE_COUNT_64_BIT;
    default:
      return VK_SAMPLE_COUNT_1_BIT;
    }
    return VK_SAMPLE_COUNT_1_BIT;
  }

  VkSampleCountFlagBits makeSamples(const ImageSize& iSize)
  {
    return makeSamples(iSize.samples);
  }

}

ImageTraits::ImageTraits(VkFormat f, ImageSize imSize, VkImageUsageFlags u, bool lt, VkImageLayout il, VkImageCreateFlags ic, VkImageType it, VkSharingMode sm)
  : format{ f }, imageSize{ imSize }, usage{ u }, linearTiling{ lt }, initialLayout{ il }, imageCreate{ ic }, imageType{ it }, sharingMode{ sm }
{
}

Image::Image(Device* d, const ImageTraits& it, std::shared_ptr<DeviceMemoryAllocator> a)
  : imageTraits{ it }, device(d->device), allocator{ a }, ownsImage{ true }
{
  VkImageCreateInfo imageCI{};
    imageCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.flags         = imageTraits.imageCreate;
    imageCI.imageType     = imageTraits.imageType;
    imageCI.format        = imageTraits.format;
    imageCI.extent        = makeVkExtent3D(imageTraits.imageSize);
    imageCI.mipLevels     = imageTraits.imageSize.mipLevels;
    imageCI.arrayLayers   = imageTraits.imageSize.arrayLayers;
    imageCI.samples       = makeSamples(imageTraits.imageSize);
    imageCI.tiling        = imageTraits.linearTiling ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage         = imageTraits.usage;
    imageCI.sharingMode   = imageTraits.sharingMode;
//    imageCI.queueFamilyIndexCount;
//    imageCI.pQueueFamilyIndices;
    imageCI.initialLayout = imageTraits.initialLayout;
  VK_CHECK_LOG_THROW(vkCreateImage(device, &imageCI, nullptr, &image), "failed vkCreateImage");

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(device, image, &memReqs);

  memoryBlock = allocator->allocate(d, memReqs);
  CHECK_LOG_THROW(memoryBlock.alignedSize == 0, "Cannot allocate memory for Image");
  VK_CHECK_LOG_THROW(vkBindImageMemory(device, image, memoryBlock.memory, memoryBlock.alignedOffset), "failed vkBindImageMemory");
}

Image::Image(Device* d, VkImage i, VkFormat format, const ImageSize& imageSize)
  : device(d->device), image{ i }, ownsImage{  false }
{
  // gather all what we know about delivered image
  imageTraits.format      = format;
  imageTraits.imageSize   = imageSize;
}

Image::~Image()
{
  if (ownsImage)
  {
    vkDestroyImage(device, image, nullptr);
    allocator->deallocate(device, memoryBlock);
  }
}

void Image::getImageSubresourceLayout(VkImageSubresource& subRes, VkSubresourceLayout& subResLayout) const
{
  // FIXME : remember - it only works when tiling is linear
  vkGetImageSubresourceLayout(device, image, &subRes, &subResLayout);
}

void* Image::mapMemory(size_t offset, size_t range, VkMemoryMapFlags flags)
{
  void* data;
  VK_CHECK_LOG_THROW(vkMapMemory(device, memoryBlock.memory, memoryBlock.alignedSize + offset, range, flags, &data), "Cannot map memory to image");
  return data;
}

void Image::unmapMemory()
{
  vkUnmapMemory(device, memoryBlock.memory);
}

TextureLoader::TextureLoader(const std::vector<std::string>& fileExtensions)
  : supportedExtensions( fileExtensions )
{
}

namespace pumex
{

ImageTraits getImageTraitsFromTexture(const gli::texture& texture, VkImageUsageFlags usage)
{
  auto t = texture.extent(0);
  return ImageTraits(vulkanFormatFromGliFormat(texture.format()), ImageSize{ isAbsolute, glm::vec3(t.x, t.y, t.z), static_cast<uint32_t>(texture.layers()), static_cast<uint32_t>(texture.levels()), 1 }, usage,
    false, VK_IMAGE_LAYOUT_UNDEFINED, 0, vulkanImageTypeFromTextureExtents(t), VK_SHARING_MODE_EXCLUSIVE);
}

VkFormat vulkanFormatFromGliFormat(gli::texture::format_type format)
{
  // Formats are almost identical. Looks like someone implemented GLI and Vulkan at the same time
  return (VkFormat)format;
}

VkImageViewType vulkanViewTypeFromGliTarget(gli::texture::target_type target)
{

  switch (target)
  {
  case gli::texture::target_type::TARGET_1D:
    return VK_IMAGE_VIEW_TYPE_1D;
  case gli::texture::target_type::TARGET_1D_ARRAY:
    return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
  case gli::texture::target_type::TARGET_2D:
    return VK_IMAGE_VIEW_TYPE_2D;
  case gli::texture::target_type::TARGET_2D_ARRAY:
    return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  case gli::texture::target_type::TARGET_3D:
    return VK_IMAGE_VIEW_TYPE_3D;
  case gli::texture::target_type::TARGET_RECT:
    return VK_IMAGE_VIEW_TYPE_2D;
  case gli::texture::target_type::TARGET_RECT_ARRAY:
    return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  case gli::texture::target_type::TARGET_CUBE:
    return VK_IMAGE_VIEW_TYPE_CUBE;
  case gli::texture::target_type::TARGET_CUBE_ARRAY:
    return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
  }
  return VK_IMAGE_VIEW_TYPE_2D;
}

VkImageType vulkanImageTypeFromTextureExtents(const gli::extent3d& extents)
{
  if (extents.z > 1)
    return VK_IMAGE_TYPE_3D;
  if (extents.y > 1)
    return VK_IMAGE_TYPE_2D;
  return VK_IMAGE_TYPE_1D;
}

VkComponentSwizzle vulkanSwizzlesFromGliSwizzles(const gli::swizzle& s)
{
  // VK_COMPONENT_SWIZZLE_IDENTITY is not represented in GLI
  switch (s)
  {
  case gli::swizzle::SWIZZLE_RED:
    return VK_COMPONENT_SWIZZLE_R;
  case gli::swizzle::SWIZZLE_GREEN:
    return VK_COMPONENT_SWIZZLE_G;
  case gli::swizzle::SWIZZLE_BLUE:
    return VK_COMPONENT_SWIZZLE_B;
  case gli::swizzle::SWIZZLE_ALPHA:
    return VK_COMPONENT_SWIZZLE_A;
  case gli::swizzle::SWIZZLE_ZERO:
    return VK_COMPONENT_SWIZZLE_ZERO;
  case gli::swizzle::SWIZZLE_ONE:
    return VK_COMPONENT_SWIZZLE_ONE;
  }
  return VK_COMPONENT_SWIZZLE_IDENTITY;
}

VkComponentMapping vulkanComponentMappingFromGliComponentMapping(const gli::swizzles& swz)
{
  VkComponentMapping mapping;
  mapping.r = vulkanSwizzlesFromGliSwizzles(swz.r);
  mapping.g = vulkanSwizzlesFromGliSwizzles(swz.g);
  mapping.b = vulkanSwizzlesFromGliSwizzles(swz.b);
  mapping.a = vulkanSwizzlesFromGliSwizzles(swz.a);
  return mapping;
}

}
