//
// Copyright(c) 2017 Pawe³ Ksiê¿opolski ( pumexx )
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
#include <vector>
#include <mutex>
#include <vulkan/vulkan.h>
#include <gli/texture.hpp>
#include <pumex/Export.h>
#include <pumex/Pipeline.h>
#include <pumex/Command.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/RenderWorkflow.h>

namespace pumex
{

class Surface;
class Image;


struct PUMEX_EXPORT FrameBufferImageDefinition
{
  FrameBufferImageDefinition(AttachmentType attachmentType, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectMask, VkSampleCountFlagBits samples, const AttachmentSize& attachmentSize = AttachmentSize(astSurfaceDependent, glm::vec2(1.0f, 1.0f)), const gli::swizzles& swizzles = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA));
  AttachmentType        attachmentType;
  VkFormat              format;
  VkImageUsageFlags     usage;
  VkImageAspectFlags    aspectMask;
  VkSampleCountFlagBits samples;
  AttachmentSize        attachmentSize;
//  Size                  sizeType;
//  glm::vec2             imageSize;
  gli::swizzles         swizzles;
};

class PUMEX_EXPORT FrameBufferImages
{
public:
  FrameBufferImages()                                    = delete;
  explicit FrameBufferImages(const std::vector<FrameBufferImageDefinition>& fbid, std::weak_ptr<DeviceMemoryAllocator> allocator);
  FrameBufferImages(const FrameBufferImages&)            = delete;
  FrameBufferImages& operator=(const FrameBufferImages&) = delete;
  virtual ~FrameBufferImages();

  void                       validate(Surface* surface);
  void                       reset(Surface* surface);
  Image*                     getImage(Surface* surface, uint32_t imageIndex);
  FrameBufferImageDefinition getSwapChainDefinition();


  std::vector<FrameBufferImageDefinition> imageDefinitions;
protected:
  struct PerSurfaceData
  {
    PerSurfaceData(VkDevice d, uint32_t imCount)
      : device{ d }
    {
      frameBufferImages.resize(imCount);
    }
    VkDevice                             device;
    std::vector<std::shared_ptr<Image>>  frameBufferImages;
    bool                                 dirty = true;
  };

  mutable std::mutex                               mutex;
  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
  std::weak_ptr<DeviceMemoryAllocator>             allocator;
};


class PUMEX_EXPORT FrameBuffer : public CommandBufferSource
{
public:
  FrameBuffer()                              = delete;
  explicit FrameBuffer( std::shared_ptr<RenderPass> renderPass, std::shared_ptr<FrameBufferImages> frameBufferImages );
  FrameBuffer(const FrameBuffer&)            = delete;
  FrameBuffer& operator=(const FrameBuffer&) = delete;
  virtual ~FrameBuffer();

  void          reset(Surface* surface);
  void          validate(Surface* surface, const std::vector<std::unique_ptr<Image>>& swapChainImages = std::vector<std::unique_ptr<Image>>());
  VkFramebuffer getFrameBuffer(Surface* surface, uint32_t fbIndex);

  std::weak_ptr<RenderPass>         renderPass;
  std::weak_ptr<FrameBufferImages>  frameBufferImages;
protected:
  struct PerSurfaceData
  {
    PerSurfaceData(VkDevice d, uint32_t fbCount)
      : device{ d }
    {
      frameBuffers.resize(fbCount, VK_NULL_HANDLE);
    }
    VkDevice                            device;
    std::vector<VkFramebuffer>          frameBuffers;
    bool                                dirty = true;
  };
  mutable std::mutex                               mutex;
  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
};

class PUMEX_EXPORT InputAttachment : public DescriptorSetSource
{
public:
  InputAttachment(std::shared_ptr<FrameBuffer> frameBuffer, uint32_t frameBufferIndex);

  void validate(std::weak_ptr<Surface> surface);
  void getDescriptorSetValues(VkSurfaceKHR surface, uint32_t index, std::vector<DescriptorSetValue>& values) const override;
protected:
  std::weak_ptr<FrameBuffer> frameBuffer;
  uint32_t frameBufferIndex;

  struct PerSurfaceData
  {
    PerSurfaceData(std::weak_ptr<Surface> s)
      : surface{ s }
    {
    }
    std::weak_ptr<Surface> surface;
    bool                   dirty = true;
  };
  mutable std::mutex                               mutex;
  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
};


}