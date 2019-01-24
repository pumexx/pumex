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
#include <cmath>
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <pumex/Export.h>

namespace pumex
{

enum ImageSizeType { isUndefined, isAbsolute, isSurfaceDependent };

// struct defining size of Image
struct PUMEX_EXPORT ImageSize
{

  ImageSize()
    : type{ isUndefined }, size{ 0.0f, 0.0f, 0.0f }, arrayLayers{ 1 }, mipLevels{ 1 }, samples{ 1 }
  {
  }
  ImageSize(ImageSizeType aType, const glm::vec2& imSize, uint32_t aLayers = 1, uint32_t mLevels = 1, uint32_t xSamples = 1)
    : type{ aType }, size{ imSize.x, imSize.y, 1.0f }, arrayLayers{ aLayers }, mipLevels{ mLevels }, samples{ xSamples }
  {
  }
  ImageSize(ImageSizeType aType, const glm::vec3& imSize, uint32_t aLayers = 1, uint32_t mLevels = 1, uint32_t xSamples = 1)
    : type{ aType }, size{ imSize }, arrayLayers{ aLayers }, mipLevels{ mLevels }, samples{ xSamples }
  {
  }

  ImageSizeType type;
  glm::vec3     size;
  uint32_t      arrayLayers;
  uint32_t      mipLevels;
  uint32_t      samples;
};

inline bool operator==(const ImageSize& lhs, const ImageSize& rhs);
inline bool operator!=(const ImageSize& lhs, const ImageSize& rhs);

PUMEX_EXPORT VkImageType vulkanImageTypeFromImageSize(const ImageSize& is);

// struct defining subresource range for buffer
struct PUMEX_EXPORT BufferSubresourceRange
{
  BufferSubresourceRange();
  BufferSubresourceRange(VkDeviceSize offset, VkDeviceSize range);

  bool contains(const BufferSubresourceRange& subRange) const;
  inline bool valid() const;

  VkDeviceSize offset;
  VkDeviceSize range;
};

inline bool operator==(const BufferSubresourceRange& lhs, const BufferSubresourceRange& rhs);
inline bool operator<(const BufferSubresourceRange& lhs, const BufferSubresourceRange& rhs);

PUMEX_EXPORT bool anyRangeOverlaps(const std::vector<BufferSubresourceRange>& ranges);
PUMEX_EXPORT bool rangeOverlaps(const BufferSubresourceRange& lhs, const BufferSubresourceRange& rhs);
PUMEX_EXPORT BufferSubresourceRange mergeRanges(const std::vector<BufferSubresourceRange>& ranges);
PUMEX_EXPORT BufferSubresourceRange mergeRange(const BufferSubresourceRange& lhs, const BufferSubresourceRange& rhs);

// struct defining subresource range for image
struct PUMEX_EXPORT ImageSubresourceRange
{
  ImageSubresourceRange();
  ImageSubresourceRange(VkImageAspectFlags aspectMask, uint32_t baseMipLevel=0, uint32_t levelCount=1, uint32_t baseArrayLayer=0, uint32_t layerCount=1);

  VkImageSubresourceRange  getSubresource() const;
  VkImageSubresourceLayers getSubresourceLayers() const;

  bool contains(const ImageSubresourceRange& subRange) const;
  inline bool valid() const;

  VkImageAspectFlags    aspectMask;
  uint32_t              baseMipLevel;
  uint32_t              levelCount;
  uint32_t              baseArrayLayer;
  uint32_t              layerCount;
};

inline bool operator==(const ImageSubresourceRange& lhs, const ImageSubresourceRange& rhs);
inline bool operator<(const ImageSubresourceRange& lhs, const ImageSubresourceRange& rhs);

PUMEX_EXPORT bool anyRangeOverlaps(const std::vector<ImageSubresourceRange>& ranges);
PUMEX_EXPORT bool rangeOverlaps(const ImageSubresourceRange& lhs, const ImageSubresourceRange& rhs);
PUMEX_EXPORT ImageSubresourceRange mergeRanges(const std::vector<ImageSubresourceRange>& ranges);
PUMEX_EXPORT ImageSubresourceRange mergeRange(const ImageSubresourceRange& lhs, const ImageSubresourceRange& rhs);

inline bool compareRenderOperationSizeWithImageSize(const ImageSize& operationSize, const ImageSize& imageSize, const ImageSubresourceRange& imageRange);

// inlines

bool operator==(const ImageSize& lhs, const ImageSize& rhs)
{
  return lhs.type == rhs.type && lhs.size == rhs.size && lhs.arrayLayers == rhs.arrayLayers && lhs.mipLevels == rhs.mipLevels;
}

bool operator!=(const ImageSize& lhs, const ImageSize& rhs)
{
  return lhs.type != rhs.type || lhs.size != rhs.size || lhs.arrayLayers != rhs.arrayLayers || lhs.mipLevels != rhs.mipLevels;
}

bool BufferSubresourceRange::valid() const
{
  return range > 0;
}

bool operator==(const BufferSubresourceRange& lhs, const BufferSubresourceRange& rhs)
{
  return (lhs.offset == rhs.offset) && (lhs.range == rhs.range);
}

bool operator<(const BufferSubresourceRange& lhs, const BufferSubresourceRange& rhs)
{
  if (lhs.offset != rhs.offset)
    return lhs.offset < rhs.offset;
  return lhs.range < rhs.range;
}

bool ImageSubresourceRange::valid() const
{
  return ( levelCount * layerCount ) > 0;
}

bool operator==(const ImageSubresourceRange& lhs, const ImageSubresourceRange& rhs)
{
  return (lhs.aspectMask == rhs.aspectMask) && (lhs.baseMipLevel == rhs.baseMipLevel) && (lhs.levelCount == rhs.levelCount) && (lhs.baseArrayLayer == rhs.baseArrayLayer) && (lhs.layerCount == rhs.layerCount);
}

bool operator<(const ImageSubresourceRange& lhs, const ImageSubresourceRange& rhs)
{
  if ( lhs.aspectMask != rhs.aspectMask )
    return lhs.aspectMask < rhs.aspectMask;
  if ( lhs.baseMipLevel != rhs.baseMipLevel )
    return lhs.baseMipLevel < rhs.baseMipLevel;
  if (lhs.baseArrayLayer != rhs.baseArrayLayer)
    return lhs.baseArrayLayer < rhs.baseArrayLayer;
  if (lhs.levelCount != rhs.levelCount)
    return lhs.levelCount < rhs.levelCount;
  return lhs.layerCount < rhs.layerCount;
}

bool compareRenderOperationSizeWithImageSize(const ImageSize& operationSize, const ImageSize& imageSize, const ImageSubresourceRange& imageRange)
{
  return operationSize.type == imageSize.type && operationSize.size.x == ( imageSize.size.x / pow(2,imageRange.baseMipLevel) ) && operationSize.size.y == ( imageSize.size.y / pow(2,imageRange.baseMipLevel) ) ;
}

}
