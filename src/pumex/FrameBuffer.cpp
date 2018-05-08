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
#include <pumex/Texture.h>
#include <pumex/utils/Log.h>

using namespace pumex;

FrameBufferImageDefinition::FrameBufferImageDefinition(AttachmentType at, VkFormat f, VkImageUsageFlags u, VkImageAspectFlags am, VkSampleCountFlagBits s, const std::string& n, const AttachmentSize& as, const gli::swizzles& sw)
  : attachmentType{ at }, format{ f }, usage{ u }, aspectMask{ am }, samples{ s }, name{ n }, attachmentSize { as }, swizzles{ sw }
{
}

FrameBuffer::FrameBuffer(const std::vector<FrameBufferImageDefinition>& fbid, std::shared_ptr<RenderPass> rp, std::shared_ptr<DeviceMemoryAllocator> a)
  : imageDefinitions(fbid), renderPass{ rp }, allocator{ a }, activeCount{ 1 }
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

  CHECK_LOG_THROW(renderPass.use_count() == 0, "FrameBuffer::validate() : render pass was not defined");

  if (pddit->second.data[activeIndex].frameBuffer != VK_NULL_HANDLE)
    vkDestroyFramebuffer(renderContext.vkDevice, pddit->second.data[activeIndex].frameBuffer, nullptr);

  for (auto& imageView : imageViews)
    imageView->validate(renderContext);

  // create frame buffer images ( render pass attachments ), skip images marked as swap chain images ( as they're created already )
  std::vector<VkImageView> iViews(imageDefinitions.size(), VK_NULL_HANDLE);
  for (uint32_t i = 0; i < imageDefinitions.size(); i++)
    iViews[i] = imageViews[i]->getImageView(renderContext);

  // define frame buffers
  VkFramebufferCreateInfo frameBufferCreateInfo{};
    frameBufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferCreateInfo.renderPass      = rp->getHandle(renderContext);
    frameBufferCreateInfo.attachmentCount = iViews.size();
    frameBufferCreateInfo.pAttachments    = iViews.data();
    frameBufferCreateInfo.width           = renderContext.surface->swapChainSize.width;
    frameBufferCreateInfo.height          = renderContext.surface->swapChainSize.height;
    frameBufferCreateInfo.layers          = 1;
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

void FrameBuffer::prepareTextures(const RenderContext& renderContext, std::vector<std::shared_ptr<Image>>& swapChainImages)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (textures.empty())
  {
    for (uint32_t i = 0; i < imageDefinitions.size(); i++)
    {
      FrameBufferImageDefinition& definition = imageDefinitions[i];
      VkExtent3D imSize{ 1,1,1 };
      ImageTraits imageTraits(definition.usage, definition.format, imSize, 1, 1, definition.samples, false, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE);
      SwapChainImageBehaviour scib = (definition.attachmentType == atSurface) ? swForEachImage : swOnce;
      auto texture = std::make_shared<Texture>(imageTraits, allocator, definition.aspectMask, pbPerSurface, scib, false, false);
      textures.push_back(texture);
      ImageSubresourceRange range(definition.aspectMask, 0, 1, 0, 1);
      imageViews.push_back(std::make_shared<ImageView>(texture, range, VK_IMAGE_VIEW_TYPE_2D));
    }
  }
  for (uint32_t i = 0; i < imageDefinitions.size(); i++)
  {
    FrameBufferImageDefinition& definition = imageDefinitions[i];
    if (definition.attachmentType == atSurface)
    {
      textures[i]->setImages(renderContext.surface, swapChainImages);
    }
    else
    {
      VkExtent3D imSize;
      switch (definition.attachmentSize.attachmentSize)
      {
      case AttachmentSize::SurfaceDependent:
      {
        imSize.width  = renderContext.surface->swapChainSize.width  * definition.attachmentSize.imageSize.x;
        imSize.height = renderContext.surface->swapChainSize.height * definition.attachmentSize.imageSize.y;
        imSize.depth  = 1;
        break;
      }
      case AttachmentSize::Absolute:
      {
        imSize.width  = definition.attachmentSize.imageSize.x;
        imSize.height = definition.attachmentSize.imageSize.y;
        imSize.depth  = 1;
        break;
      }
      }
      ImageTraits imageTraits(definition.usage, definition.format, imSize, 1, 1, definition.samples, false, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE);
      textures[i]->setImageTraits(renderContext.surface, imageTraits);
    }
  }
  auto rp = renderPass.lock();
  rp->invalidate(renderContext);
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
  textures.clear();
}

const FrameBufferImageDefinition& FrameBuffer::getSwapChainImageDefinition() const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = std::find_if(begin(imageDefinitions), end(imageDefinitions), [](const FrameBufferImageDefinition& def) { return def.attachmentType == atSurface;  });
  CHECK_LOG_THROW(it == end(imageDefinitions), "Framebuffer used by the surface does not have swapchain image defined");
  return *it;
}

const FrameBufferImageDefinition& FrameBuffer::getImageDefinition(uint32_t index) const
{
  CHECK_LOG_THROW(index >=imageDefinitions.size(), "Image definition index out of bounds");
  return imageDefinitions[index];
}

std::shared_ptr<Texture> FrameBuffer::getTexture(uint32_t index) const
{
  CHECK_LOG_THROW(index >= textures.size(), "Texture index out of bounds");
  return textures[index];
}

std::shared_ptr<ImageView> FrameBuffer::getImageView(const std::string& name) const
{
  auto it = std::find_if(begin(imageDefinitions), end(imageDefinitions), [&name](const FrameBufferImageDefinition& def) { return def.name == name; });
  if (it == end(imageDefinitions))
    return std::shared_ptr<ImageView>();
  return imageViews[std::distance(begin(imageDefinitions),it)];
}

VkFramebuffer FrameBuffer::getHandleFrameBuffer(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(renderContext.vkSurface);
  if (pddit == end(perObjectData))
    return VK_NULL_HANDLE;
  return pddit->second.data[renderContext.activeIndex % activeCount].frameBuffer;
}
