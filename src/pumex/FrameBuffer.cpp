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

FrameBufferImageDefinition::FrameBufferImageDefinition()
  : attachmentType{ atUndefined }, format{ VK_FORMAT_UNDEFINED }, usage{ 0x0 }, aspectMask{ 0x0 }, samples{ VK_SAMPLE_COUNT_1_BIT }, name{}, attachmentSize{}, swizzles{ gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA) }
{
}


FrameBufferImageDefinition::FrameBufferImageDefinition(AttachmentType at, VkFormat f, VkImageUsageFlags u, VkImageAspectFlags am, VkSampleCountFlagBits s, const std::string& n, const AttachmentSize& as, const gli::swizzles& sw)
  : attachmentType{ at }, format{ f }, usage{ u }, aspectMask{ am }, samples{ s }, name{ n }, attachmentSize { as }, swizzles{ sw }
{
}

FrameBuffer::FrameBuffer(const AttachmentSize& fbs, const std::vector<FrameBufferImageDefinition>& fbid, std::shared_ptr<RenderPass> rp, std::map<std::string, std::shared_ptr<MemoryImage>> mi, std::map<std::string, std::shared_ptr<ImageView>> iv)
  : frameBufferSize{fbs}, imageDefinitions(fbid), renderPass{ rp }, activeCount{ 1 }
{
  for (uint32_t i = 0; i < imageDefinitions.size(); i++)
  {
    FrameBufferImageDefinition& definition = imageDefinitions[i];
    CHECK_LOG_THROW(frameBufferSize != definition.attachmentSize, "FrameBuffer::FrameBuffer() : image definition size is different from framebuffer size");
    auto mit = mi.find(definition.name);
    CHECK_LOG_THROW(mit == end(mi), "FrameBuffer::FrameBuffer() : not all memory images have been supplied");
    memoryImages.push_back(mit->second);

    auto vit = iv.find(definition.name);
    CHECK_LOG_THROW(vit == end(iv), "FrameBuffer::FrameBuffer() : not all memory image views have been supplied");
    imageViews.push_back(vit->second);
  }
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
  std::vector<VkImageView> iViews(imageDefinitions.size(), VK_NULL_HANDLE);
  for (uint32_t i = 0; i < imageDefinitions.size(); i++)
    iViews[i] = imageViews[i]->getImageView(renderContext);
  // find framebuffer size from first image definition

  uint32_t frameBufferWidth, frameBufferHeight;
  switch (frameBufferSize.attachmentSize)
  {
  case AttachmentSize::SurfaceDependent:
  {
    frameBufferWidth  = renderContext.surface->swapChainSize.width  * frameBufferSize.imageSize.x;
    frameBufferHeight = renderContext.surface->swapChainSize.height * frameBufferSize.imageSize.y;
    break;
  }
  case AttachmentSize::Absolute:
  {
    frameBufferWidth  = frameBufferSize.imageSize.x;
    frameBufferHeight = frameBufferSize.imageSize.y;
    break;
  }
  default:
  {
    frameBufferWidth  = 1;
    frameBufferHeight = 1;
    break;
  }
  }

  // define frame buffers
  VkFramebufferCreateInfo frameBufferCreateInfo{};
    frameBufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferCreateInfo.renderPass      = rp->getHandle(renderContext);
    frameBufferCreateInfo.attachmentCount = iViews.size();
    frameBufferCreateInfo.pAttachments    = iViews.data();
    frameBufferCreateInfo.width           = frameBufferWidth;
    frameBufferCreateInfo.height          = frameBufferHeight;
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

void FrameBuffer::prepareMemoryImages(const RenderContext& renderContext, std::vector<std::shared_ptr<Image>>& swapChainImages)
{
  std::lock_guard<std::mutex> lock(mutex);
  for (uint32_t i = 0; i < imageDefinitions.size(); i++)
  {
    FrameBufferImageDefinition& definition = imageDefinitions[i];
    if (definition.attachmentType == atSurface)
    {
      memoryImages[i]->setImages(renderContext.surface, swapChainImages);
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
      default:
      {
        imSize.width  = 1;
        imSize.height = 1;
        imSize.depth  = 1;
        break;
      }
      }
      uint32_t layerCount = static_cast<uint32_t>(definition.attachmentSize.imageSize.z);
      ImageTraits imageTraits(definition.usage, definition.format, imSize, 1, layerCount, definition.samples, false, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE);
      memoryImages[i]->setImageTraits(renderContext.surface, imageTraits);
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
  memoryImages.clear();
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

std::shared_ptr<MemoryImage> FrameBuffer::getMemoryImage(uint32_t index) const
{
  CHECK_LOG_THROW(index >= memoryImages.size(), "Texture index out of bounds");
  return memoryImages[index];
}

std::shared_ptr<ImageView> FrameBuffer::getImageView(uint32_t index) const
{
  CHECK_LOG_THROW(index >= memoryImages.size(), "Texture index out of bounds");
  return imageViews[index];
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
