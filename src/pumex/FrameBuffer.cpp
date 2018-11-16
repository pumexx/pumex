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

FrameBuffer::FrameBuffer(const ImageSize& is, const std::vector<WorkflowResource>& ar, std::shared_ptr<RenderPass> rp, std::map<std::string, std::shared_ptr<MemoryImage>> mi, std::map<std::string, std::shared_ptr<ImageView>> iv)
  : frameBufferSize{is}, attachmentResources(ar), renderPass{ rp }, activeCount{ 1 }
{
  for( const auto& resource : attachmentResources)
  {
    CHECK_LOG_THROW(frameBufferSize != resource.resourceType->attachment.attachmentSize, "FrameBuffer::FrameBuffer() : image definition size is different from framebuffer size");
    auto mit = mi.find(resource.name);
    CHECK_LOG_THROW(mit == end(mi), "FrameBuffer::FrameBuffer() : not all memory images have been supplied");
    memoryImages.push_back(mit->second);

    auto vit = iv.find(resource.name);
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
  std::vector<VkImageView> iViews(attachmentResources.size(), VK_NULL_HANDLE);
  for (uint32_t i = 0; i < attachmentResources.size(); i++)
    iViews[i] = imageViews[i]->getImageView(renderContext);
  // find framebuffer size from first image definition

  VkExtent2D extent;
  switch (frameBufferSize.type)
  {
  case ImageSize::SurfaceDependent:
  {
    extent = makeVkExtent2D(frameBufferSize, renderContext.surface->swapChainSize);
    break;
  }
  case ImageSize::Absolute:
  {
    extent = makeVkExtent2D(frameBufferSize);
    break;
  }
  default:
  {
    extent = VkExtent2D{ 1,1 };
    break;
  }
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

void FrameBuffer::prepareMemoryImages(const RenderContext& renderContext, std::vector<std::shared_ptr<Image>>& swapChainImages)
{
  std::lock_guard<std::mutex> lock(mutex);
  for (uint32_t i = 0; i < attachmentResources.size(); i++)
  {
    WorkflowResource& resource = attachmentResources[i];
    if (resource.resourceType->attachment.attachmentType == atSurface)
    {
      memoryImages[i]->setImages(renderContext.surface, swapChainImages);
    }
    else
    {
      ImageSize imageSize{ resource.resourceType->attachment.attachmentSize };
      if(imageSize.type == ImageSize::SurfaceDependent)
        imageSize.size *= glm::vec3(renderContext.surface->swapChainSize.width, renderContext.surface->swapChainSize.height, 1);
      ImageTraits imageTraits(resource.resourceType->attachment.format, imageSize, resource.resourceType->attachment.imageUsage, false, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE);
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

const WorkflowResource& FrameBuffer::getAttachmentResource(uint32_t index) const
{
  CHECK_LOG_THROW(index >= attachmentResources.size(), "Image definition index out of bounds");
  return attachmentResources[index];
}

const WorkflowResource& FrameBuffer::getSwapChainAttachmentResource() const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = std::find_if(begin(attachmentResources), end(attachmentResources), [](const WorkflowResource& def) { return def.resourceType->attachment.attachmentType == atSurface;  });
  CHECK_LOG_THROW(it == end(attachmentResources), "Framebuffer used by the surface does not have swapchain image defined");
  return *it;
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
  auto it = std::find_if(begin(attachmentResources), end(attachmentResources), [&name](const WorkflowResource& def) { return def.name == name; });
  if (it == end(attachmentResources))
    return std::shared_ptr<ImageView>();
  return imageViews[std::distance(begin(attachmentResources),it)];
}

VkFramebuffer FrameBuffer::getHandleFrameBuffer(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(renderContext.vkSurface);
  if (pddit == end(perObjectData))
    return VK_NULL_HANDLE;
  return pddit->second.data[renderContext.activeIndex % activeCount].frameBuffer;
}
