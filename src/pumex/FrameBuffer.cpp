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

#include <pumex/FrameBuffer.h>
#include <pumex/RenderPass.h>
#include <pumex/Texture.h>
#include <pumex/Surface.h>
#include <pumex/Command.h>
#include <pumex/utils/Log.h>

namespace pumex
{

FrameBufferImageDefinition::FrameBufferImageDefinition(AttachmentType at, VkFormat f, VkImageUsageFlags u, VkImageAspectFlags am, VkSampleCountFlagBits s, const AttachmentSize& as, const gli::swizzles& sw)
  : attachmentType{ at }, format{ f }, usage{ u }, aspectMask{ am }, samples{ s }, attachmentSize{ as }, swizzles{ sw }
{
}

FrameBufferImages::FrameBufferImages(const std::vector<FrameBufferImageDefinition>& fbid, std::weak_ptr<DeviceMemoryAllocator> a)
  : imageDefinitions(fbid), allocator{ a }
{
}

FrameBufferImages::~FrameBufferImages()
{
  for (auto it : perSurfaceData)
    it.second.frameBufferImages.clear();
}


void FrameBufferImages::validate(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(surface->device.lock()->device,imageDefinitions.size()) }).first;
  if (!pddit->second.dirty)
    return;

  for (uint32_t i = 0; i < imageDefinitions.size(); i++)
  {
    FrameBufferImageDefinition& definition = imageDefinitions[i];
    if (definition.attachmentType == atSurface)
      continue;
    VkExtent3D imSize;
    switch (definition.attachmentSize.attachmentSize)
    {
    case astSurfaceDependent:
    {
      imSize.width  = surface->swapChainSize.width  * definition.attachmentSize.imageSize.x;
      imSize.height = surface->swapChainSize.height * definition.attachmentSize.imageSize.y;
      imSize.depth  = 1;
      break;
    }
    case astAbsolute:
    {
      imSize.width  = definition.attachmentSize.imageSize.x;
      imSize.height = definition.attachmentSize.imageSize.y;
      imSize.depth  = 1;
      break;
    }
    }
    ImageTraits imageTraits(definition.usage, definition.format, imSize, false, 1, 1,
      definition.samples, VK_IMAGE_LAYOUT_UNDEFINED, definition.aspectMask, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE,
      VK_IMAGE_VIEW_TYPE_2D, definition.swizzles);
    pddit->second.frameBufferImages[i] = std::make_unique<Image>(surface->device.lock().get(), imageTraits, allocator);
  }
  pddit->second.dirty = false;
}

void FrameBufferImages::reset(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit != perSurfaceData.end())
  {
    pddit->second.frameBufferImages.clear();
    perSurfaceData.erase(surface->surface);
  }
}

Image* FrameBufferImages::getImage(Surface* surface, uint32_t imageIndex)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    return nullptr;
  if (pddit->second.frameBufferImages.size() <= imageIndex)
    return nullptr;
  return pddit->second.frameBufferImages[imageIndex].get();
}

FrameBufferImageDefinition FrameBufferImages::getSwapChainDefinition()
{
  for (const auto& d : imageDefinitions)
    if (d.attachmentType == atSurface)
      return d;
  return FrameBufferImageDefinition(atSurface, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT);
}

FrameBuffer::FrameBuffer(std::shared_ptr<RenderPass> rp, std::shared_ptr<FrameBufferImages> fbi)
  : renderPass{ rp }, frameBufferImages{ fbi }
{
}

FrameBuffer::~FrameBuffer()
{
  for (auto it : perSurfaceData)
  {
    for (auto f : it.second.frameBuffers)
      vkDestroyFramebuffer(it.second.device, f, nullptr);
  }
}

void FrameBuffer::reset(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit != perSurfaceData.end())
  {
    for (auto f : pddit->second.frameBuffers)
      vkDestroyFramebuffer(pddit->second.device, f, nullptr);
    perSurfaceData.erase(surface->surface);
  }
}

void FrameBuffer::validate(Surface* surface, const std::vector<std::unique_ptr<Image>>& swapChainImages)
{
  std::lock_guard<std::mutex> lock(mutex);
  std::shared_ptr<RenderPass> rp         = renderPass.lock();
  std::shared_ptr<Device> deviceSh       = surface->device.lock();
  std::shared_ptr<FrameBufferImages> fbi = frameBufferImages.lock();

  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(surface->device.lock()->device, swapChainImages.empty()? 1 : swapChainImages.size() ) }).first;
  if (!pddit->second.dirty)
    return;

  // create frame buffer images ( render pass attachments ), skip images marked as swap chain images ( as they're created already )
  std::vector<VkImageView> imageViews;
  imageViews.resize(rp->attachments.size());
  for (uint32_t i = 0; i < rp->attachments.size(); i++)
  {
    AttachmentDefinition& definition          = rp->attachments[i];
    FrameBufferImageDefinition& fbiDefinition = fbi->imageDefinitions[definition.imageDefinitionIndex];
    if (fbiDefinition.attachmentType == atSurface)
    {
      imageViews[i] = VK_NULL_HANDLE;
      continue;
    }
    else
    {
      imageViews[i] = fbi->getImage(surface, definition.imageDefinitionIndex)->getImageView();
    }

    if (definition.initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
    {
      auto commandBuffer = deviceSh->beginSingleTimeCommands(surface->commandPool.get());
      commandBuffer->setImageLayout(*(fbi->getImage(surface, definition.imageDefinitionIndex)), fbiDefinition.aspectMask, VK_IMAGE_LAYOUT_UNDEFINED, definition.initialLayout);
      deviceSh->endSingleTimeCommands(commandBuffer, surface->presentationQueue);
    }
  }

  // create frame buffer for each swap chain image
  for (uint32_t i = 0; i < pddit->second.frameBuffers.size(); i++)
  {
    for (uint32_t j = 0; j < rp->attachments.size(); j++)
    {
      AttachmentDefinition& definition          = rp->attachments[j];
      FrameBufferImageDefinition& fbiDefinition = fbi->imageDefinitions[definition.imageDefinitionIndex];
      if (fbiDefinition.attachmentType == atSurface)
        imageViews[j] = swapChainImages[i]->getImageView();
    }

    // define frame buffers
    VkFramebufferCreateInfo frameBufferCreateInfo{};
      frameBufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      frameBufferCreateInfo.renderPass      = rp->getHandle(deviceSh->device);
      frameBufferCreateInfo.attachmentCount = imageViews.size();
      frameBufferCreateInfo.pAttachments    = imageViews.data();
      frameBufferCreateInfo.width           = surface->swapChainSize.width;
      frameBufferCreateInfo.height          = surface->swapChainSize.height;
      frameBufferCreateInfo.layers          = 1;
    VK_CHECK_LOG_THROW(vkCreateFramebuffer(deviceSh->device, &frameBufferCreateInfo, nullptr, &pddit->second.frameBuffers[i]), "Could not create frame buffer " << i);
  }
  pddit->second.dirty = false;
  notifyCommandBuffers();
}

VkFramebuffer FrameBuffer::getFrameBuffer(Surface* surface, uint32_t fbIndex)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    return VK_NULL_HANDLE;
  if (pddit->second.frameBuffers.size() <= fbIndex)
    return VK_NULL_HANDLE;
  return pddit->second.frameBuffers[fbIndex];
}

InputAttachment::InputAttachment(std::shared_ptr<FrameBuffer> fb, uint32_t fbi)
  : frameBuffer{ fb }, frameBufferIndex{ fbi }
{
}

void InputAttachment::validate(std::weak_ptr<Surface> surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  std::shared_ptr<Surface> s = surface.lock();
  auto pddit = perSurfaceData.find(s->surface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ s->surface, PerSurfaceData(surface) }).first;
  if (!pddit->second.dirty)
    return;
  pddit->second.dirty = false;
}

void InputAttachment::getDescriptorSetValues(VkSurfaceKHR surface, uint32_t index, std::vector<DescriptorSetValue>& values) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface);
  if (pddit == perSurfaceData.end())
    return;
  std::shared_ptr<Surface>     s = pddit->second.surface.lock();
  std::shared_ptr<FrameBuffer> f = frameBuffer.lock();
  if (f.get() != nullptr)
  {
    std::shared_ptr<RenderPass> rp         = f->renderPass.lock();
    std::shared_ptr<FrameBufferImages> fbi = f->frameBufferImages.lock();
    uint32_t actualIndex = rp->attachments[frameBufferIndex].imageDefinitionIndex;
    values.push_back(DescriptorSetValue(VK_NULL_HANDLE, fbi->getImage(s.get(), frameBufferIndex)->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
  }
  else
  {
    std::shared_ptr<RenderPass> rp         = s->frameBuffer->renderPass.lock();
    std::shared_ptr<FrameBufferImages> fbi = s->frameBuffer->frameBufferImages.lock();
    uint32_t actualIndex = rp->attachments[frameBufferIndex].imageDefinitionIndex;
    values.push_back(DescriptorSetValue(VK_NULL_HANDLE, fbi->getImage(s.get(), frameBufferIndex)->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
  }
}

}