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
#include <pumex/ResourceRange.h>
#include <pumex/utils/Log.h>
#include <list>
using namespace pumex;

BufferSubresourceRange::BufferSubresourceRange()
  : offset{ 0 }, range{ VK_WHOLE_SIZE }
{
}

BufferSubresourceRange::BufferSubresourceRange(VkDeviceSize o, VkDeviceSize r)
  : offset{ o }, range{ r }
{
}

bool BufferSubresourceRange::contains(const BufferSubresourceRange& subRange) const
{
  return (offset <= subRange.offset) && (offset + range >= subRange.offset + subRange.range);
}

// VK_REMAINING_MIP_LEVELS, VK_REMAINING_ARRAY_LAYERS
ImageSubresourceRange::ImageSubresourceRange()
  : aspectMask{ VK_IMAGE_ASPECT_COLOR_BIT }, baseMipLevel{ 0 }, levelCount{ 1 }, baseArrayLayer{ 0 }, layerCount{ 1 }
{
}

ImageSubresourceRange::ImageSubresourceRange(VkImageAspectFlags am, uint32_t m0, uint32_t mc, uint32_t a0, uint32_t ac)
  : aspectMask{ am }, baseMipLevel{ m0 }, levelCount{ mc }, baseArrayLayer{ a0 }, layerCount{ ac }
{
}

VkImageSubresourceRange ImageSubresourceRange::getSubresource() const
{
  VkImageSubresourceRange result;
    result.aspectMask     = aspectMask;
    result.baseMipLevel   = baseMipLevel;
    result.levelCount     = levelCount;
    result.baseArrayLayer = baseArrayLayer;
    result.layerCount     = layerCount;
  return result;
}

VkImageSubresourceLayers ImageSubresourceRange::getSubresourceLayers() const
{
  CHECK_LOG_THROW(levelCount != 1, "Cannot create VkImageSubresourceLayers when levelCount != 1");
  VkImageSubresourceLayers result;
    result.aspectMask     = aspectMask;
    result.mipLevel       = baseMipLevel;
    result.baseArrayLayer = baseArrayLayer;
    result.layerCount     = layerCount;
  return result;
}

bool ImageSubresourceRange::contains(const ImageSubresourceRange& subRange) const
{
  bool mipmapContains = (baseMipLevel <= subRange.baseMipLevel) && (baseMipLevel + levelCount >= subRange.baseMipLevel + subRange.levelCount);
  bool arrayContains  = (baseArrayLayer <= subRange.baseArrayLayer) && (baseArrayLayer + layerCount >= subRange.baseArrayLayer + subRange.layerCount);
  return mipmapContains && arrayContains;
}

namespace pumex
{

  bool anyRangeOverlaps(const std::vector<BufferSubresourceRange>& ranges)
  {
    for (auto& lhs : ranges)
      for (auto& rhs : ranges)
        if (rangeOverlaps(lhs, rhs))
          return true;
    return false;
  }

  bool rangeOverlaps(const BufferSubresourceRange& lhs, const BufferSubresourceRange& rhs)
  {
    return (lhs.offset < rhs.offset) ? (lhs.offset + lhs.range > rhs.offset) : (rhs.offset + rhs.range > lhs.offset);
  }

  BufferSubresourceRange mergeRanges(const std::vector<BufferSubresourceRange>& ranges)
  {
    // check fast paths
    if (ranges.empty())
      return BufferSubresourceRange(0, 0);
    if (ranges.size() == 1)
      return ranges[0];

    std::list<BufferSubresourceRange> rangeList;
    std::copy(begin(ranges), end(ranges), std::back_inserter(rangeList));
    rangeList.sort();
    while (rangeList.size() > 1)
    {
      bool mergedAnyRanges = false;
      std::list<BufferSubresourceRange> rangeList2;
      rangeList2.push_back(rangeList.front());
      rangeList.pop_front();
      while (!rangeList.empty())
      {
        auto& lhs = rangeList2.back();
        auto rhs = rangeList.front();
        rangeList.pop_front();
        auto merged = mergeRange(lhs, rhs);
        if (merged.valid())
        {
          rangeList2.pop_back();
          rangeList2.push_back(merged);
          mergedAnyRanges = true;
        }
        else
          rangeList2.push_back(rhs);
      }
      rangeList = rangeList2;
      if (!mergedAnyRanges)
        break;
    }
    if (rangeList.size() > 1)
      return BufferSubresourceRange(0, 0);
    return rangeList.front();
  }

  BufferSubresourceRange mergeRange(const BufferSubresourceRange& lhs, const BufferSubresourceRange& rhs)
  {
    if (lhs.offset + lhs.range == rhs.offset)
      return BufferSubresourceRange(lhs.offset, rhs.offset + rhs.range);
    return BufferSubresourceRange(0, 0);
  }

  bool anyRangeOverlaps(const std::vector<ImageSubresourceRange>& ranges)
  {
    for (auto lhs=begin(ranges); lhs!=end(ranges); ++lhs)
      for (auto rhs = lhs+1; rhs != end(ranges); ++rhs)
        if (rangeOverlaps(*lhs, *rhs))
          return true;
    return false;
  }

  bool rangeOverlaps(const ImageSubresourceRange& lhs, const ImageSubresourceRange& rhs)
  {
    bool mipmapOverlaps = (lhs.baseMipLevel < rhs.baseMipLevel) ? (lhs.baseMipLevel + lhs.levelCount > rhs.baseMipLevel) : (rhs.baseMipLevel + rhs.levelCount > lhs.baseMipLevel);
    bool arrayOverlaps  = (lhs.baseArrayLayer < rhs.baseArrayLayer) ? (lhs.baseArrayLayer + lhs.layerCount > rhs.baseArrayLayer) : (rhs.baseArrayLayer + rhs.layerCount > lhs.baseArrayLayer);
    return mipmapOverlaps && arrayOverlaps;
  }

  ImageSubresourceRange mergeRanges(const std::vector<ImageSubresourceRange>& ranges)
  {
    // check fast paths
    if(ranges.empty())
      return ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 0, 0);
    if (ranges.size() == 1)
      return ranges[0];

    std::list<ImageSubresourceRange> rangeList;
    std::copy(begin(ranges), end(ranges), std::back_inserter(rangeList));
    rangeList.sort();
    while (rangeList.size() > 1)
    {
      bool mergedAnyRanges = false;
      std::list<ImageSubresourceRange> rangeList2;
      rangeList2.push_back(rangeList.front());
      rangeList.pop_front();
      while(!rangeList.empty())
      {
        auto& lhs = rangeList2.back();
        auto rhs = rangeList.front();
        rangeList.pop_front();
        auto merged = mergeRange(lhs, rhs);
        if (merged.valid())
        {
          rangeList2.pop_back();
          rangeList2.push_back(merged);
          mergedAnyRanges = true;
        }
        else
          rangeList2.push_back(rhs);
      }
      rangeList = rangeList2;
      if (!mergedAnyRanges)
        break;
    }
    if( rangeList.size() > 1 )
      return ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 0, 0);
    return rangeList.front();
  }

  ImageSubresourceRange mergeRange(const ImageSubresourceRange& lhs, const ImageSubresourceRange& rhs)
  {
    if (lhs.aspectMask != rhs.aspectMask)
      return ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT,0,0,0,0);
    if(lhs.baseMipLevel+lhs.levelCount == rhs.baseMipLevel && lhs.baseArrayLayer == rhs.baseArrayLayer && lhs.layerCount == rhs.layerCount)
      return ImageSubresourceRange(
        lhs.aspectMask, 
        lhs.baseMipLevel, 
        lhs.levelCount + rhs.levelCount, 
        lhs.baseArrayLayer, 
        lhs.layerCount);
    if (lhs.baseArrayLayer + lhs.layerCount == rhs.baseArrayLayer && lhs.baseMipLevel == rhs.baseMipLevel && lhs.levelCount == rhs.levelCount)
      return ImageSubresourceRange(
        lhs.aspectMask,
        lhs.baseMipLevel,
        lhs.levelCount,
        lhs.baseArrayLayer,
        lhs.layerCount + rhs.layerCount);
    return ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 0, 0);
  }
  
  VkImageType vulkanImageTypeFromImageSize(const ImageSize& is)
  {
    if (is.size.z > 1)
      return VK_IMAGE_TYPE_3D;
    if (is.type == isSurfaceDependent || is.size.y > 1)
      return VK_IMAGE_TYPE_2D;
    return VK_IMAGE_TYPE_1D;
  }

}