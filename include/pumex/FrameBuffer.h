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
#include <pumex/RenderGraphExecution.h>
#include <pumex/PerObjectData.h>
#include <pumex/Image.h>

namespace pumex
{

class  Surface;
class  DeviceMemoryAllocator;
class  RenderPass;
struct AttachmentDescription;
class  MemoryImage;
class  ImageView;

class PUMEX_EXPORT FrameBuffer : public CommandBufferSource
{
public:
  FrameBuffer()                              = delete;
  explicit FrameBuffer(const ImageSize& frameBufferSize, std::shared_ptr<RenderPass> renderPass, const std::map<uint32_t, RenderGraphImageInfo>& imageInfo, const std::map<uint32_t, std::shared_ptr<MemoryImage>>& memoryImages, const std::map<uint32_t, std::shared_ptr<ImageView>>& ImageViews);
  FrameBuffer(const FrameBuffer&)            = delete;
  FrameBuffer& operator=(const FrameBuffer&) = delete;
  FrameBuffer(FrameBuffer&&)                 = delete;
  FrameBuffer& operator=(FrameBuffer&&)      = delete;
  virtual ~FrameBuffer();

  void                               validate(const RenderContext& renderContext);
  void                               invalidate(const RenderContext& renderContext);
  void                               prepareMemoryImages(const RenderContext& renderContext, std::vector<std::shared_ptr<Image>>& swapChainImages);
  void                               reset(Surface* surface);
  std::map<uint32_t, uint32_t>       getAttachmentOrder() const;
  std::vector<AttachmentDescription> getAttachmentDescription() const;
  VkFramebuffer                      getHandleFrameBuffer(const RenderContext& renderContext) const;

protected:
  struct FrameBufferInternal
  {
    FrameBufferInternal()
      : frameBuffer{ VK_NULL_HANDLE }
    {}
    VkFramebuffer frameBuffer;
  };
  typedef PerObjectData<FrameBufferInternal, uint32_t> FrameBufferData;

  std::unordered_map<VkSurfaceKHR, FrameBufferData> perObjectData;

  ImageSize                                         frameBufferSize;
  std::weak_ptr<RenderPass>                         renderPass;
  std::map<uint32_t, RenderGraphImageInfo>          imageInfo;
  std::map<uint32_t, std::shared_ptr<MemoryImage>>  memoryImages;
  std::map<uint32_t, std::shared_ptr<ImageView>>    imageViews;
  mutable std::mutex                                mutex;
  uint32_t                                          activeCount;
};

}
