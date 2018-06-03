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
#include <memory>
#include <vulkan/vulkan.h>
#include <memory>
#include <pumex/Export.h>
#include <pumex/MemoryObject.h>
#include <pumex/MemoryBuffer.h>
#include <pumex/MemoryImage.h>

namespace pumex
{

class PUMEX_EXPORT MemoryObjectBarrier
{
public:
  MemoryObjectBarrier()                                      = delete;
  MemoryObjectBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, std::shared_ptr<MemoryImage> memoryImage, VkImageLayout oldLayout, VkImageLayout newLayout, const ImageSubresourceRange& imageRange);
  MemoryObjectBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, std::shared_ptr<MemoryBuffer> memoryBuffer, const BufferSubresourceRange& bufferRange);
  MemoryObjectBarrier(const MemoryObjectBarrier&);
  MemoryObjectBarrier& operator=(const MemoryObjectBarrier&);
  ~MemoryObjectBarrier();

  MemoryObject::Type        objectType;
  VkAccessFlags             srcAccessMask;
  VkAccessFlags             dstAccessMask;
  uint32_t                  srcQueueFamilyIndex;
  uint32_t                  dstQueueFamilyIndex;

  struct ImageData
  {
    ImageData(std::shared_ptr<MemoryImage> mi, VkImageLayout ol, VkImageLayout nl, const ImageSubresourceRange& ir)
      : memoryImage{ mi }, oldLayout{ ol }, newLayout{ nl }, imageRange{ ir }
    {
    }
    std::shared_ptr<MemoryImage> memoryImage;
    VkImageLayout                oldLayout;
    VkImageLayout                newLayout;
    ImageSubresourceRange        imageRange;
  };

  struct BufferData
  {
    BufferData(std::shared_ptr<MemoryBuffer> mb, const BufferSubresourceRange& br)
      : memoryBuffer{ mb }, bufferRange{ br }
    {
    }
    std::shared_ptr<MemoryBuffer> memoryBuffer;
    BufferSubresourceRange        bufferRange;
  };

  union
  {
    ImageData  image;
    BufferData buffer;
  };

};

struct PUMEX_EXPORT MemoryObjectBarrierGroup
{
  MemoryObjectBarrierGroup(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags);

  VkPipelineStageFlags      srcStageMask;
  VkPipelineStageFlags      dstStageMask;
  VkDependencyFlags         dependencyFlags;
};

inline bool operator<(const MemoryObjectBarrierGroup& lhs, const MemoryObjectBarrierGroup& rhs)
{
  if (lhs.srcStageMask != rhs.srcStageMask)
    return lhs.srcStageMask < rhs.srcStageMask;
  if (lhs.dstStageMask != rhs.dstStageMask)
    return lhs.dstStageMask < rhs.dstStageMask;
  return lhs.dependencyFlags < rhs.dependencyFlags;
}


}