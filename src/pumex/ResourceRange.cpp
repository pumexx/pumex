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

ImageSubresourceRange::ImageSubresourceRange()
  : aspectMask{ VK_IMAGE_ASPECT_COLOR_BIT }, baseMipLevel{ 0 }, levelCount{ VK_REMAINING_MIP_LEVELS }, baseArrayLayer{ 0 }, layerCount{ VK_REMAINING_ARRAY_LAYERS }
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

bool ImageSubresourceRange::contains(const ImageSubresourceRange& subRange) const
{
  // check layers
  if ((baseArrayLayer > subRange.baseArrayLayer) || (baseArrayLayer + layerCount < subRange.baseArrayLayer + subRange.layerCount))
    return false;
  // check mip levels
  if ((baseMipLevel > subRange.baseMipLevel) || (baseMipLevel + levelCount < subRange.baseMipLevel + subRange.levelCount))
    return false;
  return true;
}
