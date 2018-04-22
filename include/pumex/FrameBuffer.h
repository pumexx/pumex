//
// Copyright(c) 2017-2018 Pawe³ Ksiê¿opolski ( pumexx )
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
#include <set>
#include <mutex>
#include <vulkan/vulkan.h>
#include <gli/texture.hpp>
#include <pumex/Export.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/RenderWorkflow.h>
#include <pumex/Image.h>
#include <pumex/Resource.h>

namespace pumex
{

class Surface;
class RenderPass;
class Sampler;

struct PUMEX_EXPORT FrameBufferImageDefinition
{
  FrameBufferImageDefinition(AttachmentType attachmentType, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectMask, VkSampleCountFlagBits samples, const std::string& name, const AttachmentSize& attachmentSize = AttachmentSize(AttachmentSize::SurfaceDependent, glm::vec2(1.0f, 1.0f)), const gli::swizzles& swizzles = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA));
  AttachmentType        attachmentType;
  VkFormat              format;
  VkImageUsageFlags     usage;
  VkImageAspectFlags    aspectMask;
  VkSampleCountFlagBits samples;
  std::string           name;
  AttachmentSize        attachmentSize;
  gli::swizzles         swizzles;
};

class PUMEX_EXPORT FrameBufferImages
{
public:
  FrameBufferImages()                                    = delete;
  explicit FrameBufferImages(const std::vector<FrameBufferImageDefinition>& fbid, std::shared_ptr<DeviceMemoryAllocator> allocator);
  FrameBufferImages(const FrameBufferImages&)            = delete;
  FrameBufferImages& operator=(const FrameBufferImages&) = delete;
  virtual ~FrameBufferImages();

  void                       invalidate(Surface* surface);
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
    bool                                 valid = false;
  };

  mutable std::mutex                               mutex;
  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
  std::shared_ptr<DeviceMemoryAllocator>           allocator;
};

class InputAttachment;

class PUMEX_EXPORT FrameBuffer : public CommandBufferSource
{
public:
  explicit FrameBuffer(std::shared_ptr<Surface> surface, uint32_t cbCount = 1);
  FrameBuffer(const FrameBuffer&)            = delete;
  FrameBuffer& operator=(const FrameBuffer&) = delete;
  virtual ~FrameBuffer();

  void setFrameBufferImages(std::shared_ptr<FrameBufferImages> frameBufferImages);
  inline std::shared_ptr<FrameBufferImages> getFrameBufferImages();

  void setRenderPass(std::shared_ptr<RenderPass> renderPass);

  void          reset();
  void          invalidate();
  void          validate(uint32_t index, const std::vector<std::unique_ptr<Image>>& swapChainImages = std::vector<std::unique_ptr<Image>>());
  VkFramebuffer getFrameBuffer(uint32_t index);

  void addInputAttachment(std::shared_ptr<InputAttachment> inputAttachment);

protected:
  std::weak_ptr<Surface>              surface;
  std::vector<bool>                   valid;
  std::vector<VkFramebuffer>          frameBuffers;

  mutable std::mutex                  mutex;
  std::weak_ptr<RenderPass>           renderPass;
  std::shared_ptr<FrameBufferImages>  frameBufferImages;

  // framebuffer has a list of input attachments that need to be invalidated when framebuffer is recreated
  std::vector<std::weak_ptr<InputAttachment>> inputAttachments;
  void invalidateInputAttachments();
};

class PUMEX_EXPORT InputAttachment : public Resource
{
public:
  InputAttachment(const std::string& attachmentName, std::shared_ptr<Sampler> sampler = nullptr);
  virtual ~InputAttachment();

  std::pair<bool, VkDescriptorType> getDefaultDescriptorType() override;
  void                              validate(const RenderContext& renderContext) override;
  void                              invalidate() override;
  DescriptorSetValue                getDescriptorSetValue(const RenderContext& renderContext) override;

protected:
  struct PerSurfaceData
  {
    PerSurfaceData()
    {
    }
    bool valid = false;
  };
  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
  std::string                                      attachmentName;
  std::shared_ptr<Sampler>                         sampler;
};

std::shared_ptr<FrameBufferImages> FrameBuffer::getFrameBufferImages() { return frameBufferImages; }

}