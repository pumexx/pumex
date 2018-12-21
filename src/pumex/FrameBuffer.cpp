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

#include <pumex/FrameBuffer.h>
#include <algorithm>
#include <pumex/Surface.h>
#include <pumex/RenderPass.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/RenderContext.h>
#include <pumex/Image.h>
#include <pumex/MemoryImage.h>
#include <pumex/utils/Log.h>

using namespace pumex;

FrameBuffer::FrameBuffer(const ImageSize& is, std::shared_ptr<RenderPass> rp, const std::vector<std::shared_ptr<ImageView>>& iv)
  : frameBufferSize{ is }, renderPass{ rp }, imageViews{ iv }, activeCount{ 1 }
{
}

FrameBuffer::~FrameBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  for( auto& pdd : perObjectData)
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
      vkDestroyFramebuffer(pdd.second.device, pdd.second.data[i].frameBuffer, nullptr);
}

void FrameBuffer::validate(const RenderContext& renderContext)
{
  CHECK_LOG_THROW(renderPass.use_count() == 0, "FrameBuffer::validate() : render pass was not defined");
  auto rp = renderPass.lock();
  rp->validate(renderContext);

  std::lock_guard<std::mutex> lock(mutex);
  if (renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto pddit = perObjectData.find(renderContext.vkSurface);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ renderContext.vkSurface, FrameBufferData(renderContext.vkDevice, renderContext.vkSurface, activeCount, swForEachImage) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  if (pddit->second.data[activeIndex].frameBuffer != VK_NULL_HANDLE)
    vkDestroyFramebuffer(renderContext.vkDevice, pddit->second.data[activeIndex].frameBuffer, nullptr);

  for (auto& imageView : imageViews)
    imageView->validate(renderContext);

  // create frame buffer images ( render pass attachments ), skip images marked as swap chain images ( as they're created already )
  std::vector<VkImageView> iViews;
  for (auto& imageView : imageViews)
    iViews.push_back(imageView->getImageView(renderContext));

  // find framebuffer size from first image definition
  VkExtent2D extent;
  switch (frameBufferSize.type)
  {
  case isSurfaceDependent:
    extent = makeVkExtent2D(frameBufferSize, renderContext.surface->swapChainSize); break;
  case isAbsolute:
    extent = makeVkExtent2D(frameBufferSize); break;
  default:
    extent = VkExtent2D{ 1,1 }; break;
  }

  // define frame buffers
  VkFramebufferCreateInfo frameBufferCreateInfo{};
    frameBufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferCreateInfo.renderPass      = rp->getHandle(renderContext);
    frameBufferCreateInfo.attachmentCount = iViews.size();
    frameBufferCreateInfo.pAttachments    = iViews.data();
    frameBufferCreateInfo.width           = extent.width;
    frameBufferCreateInfo.height          = extent.height;
    frameBufferCreateInfo.layers          = frameBufferSize.arrayLayers;
  VK_CHECK_LOG_THROW(vkCreateFramebuffer(renderContext.vkDevice, &frameBufferCreateInfo, nullptr, &pddit->second.data[activeIndex].frameBuffer), "Could not create frame buffer " << activeIndex);
  pddit->second.valid[activeIndex] = true;
  notifyCommandBuffers();
}

void FrameBuffer::invalidate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(renderContext.vkSurface);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ renderContext.vkSurface, FrameBufferData(renderContext.vkDevice, renderContext.vkSurface, activeCount, swForEachImage) }).first;
  pddit->second.invalidate();
}

void FrameBuffer::reset(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(surface->surface);
  if (pddit == end(perObjectData))
    return;
  for (uint32_t i = 0; i < pddit->second.data.size(); ++i)
  {
    vkDestroyFramebuffer(pddit->second.device, pddit->second.data[i].frameBuffer, nullptr);
    pddit->second.data[i].frameBuffer = VK_NULL_HANDLE;
  }
  imageViews.clear();
}

VkFramebuffer FrameBuffer::getHandleFrameBuffer(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(renderContext.vkSurface);
  if (pddit == end(perObjectData))
    return VK_NULL_HANDLE;
  return pddit->second.data[renderContext.activeIndex % activeCount].frameBuffer;
}
