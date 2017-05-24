#include <pumex/FrameBuffer.h>
#include <pumex/Texture.h>
#include <pumex/Surface.h>
#include <pumex/Command.h>
#include <pumex/utils/Log.h>

namespace pumex
{

FrameBufferImageDefinition::FrameBufferImageDefinition(Type t, VkFormat f, VkImageUsageFlags u, VkImageAspectFlags am, VkSampleCountFlagBits s, const gli::swizzles& sw)
  : type{ t }, format{ f }, usage{ u }, aspectMask{ am }, samples{ s }, swizzles{ sw }
{
}

FrameBufferImages::FrameBufferImages(const std::vector<FrameBufferImageDefinition>& fbid)
  : imageDefinitions(fbid)
{
}

FrameBufferImages::~FrameBufferImages()
{
  for (auto it : perSurfaceData)
    it.second.frameBufferImages.clear();
}


void FrameBufferImages::validate(std::shared_ptr<Surface> surface)
{
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(surface,imageDefinitions.size()) }).first;
  if (!pddit->second.dirty)
    return;
  std::shared_ptr<Device> deviceSh = surface->device.lock();

  for (uint32_t i = 0; i < imageDefinitions.size(); i++)
  {
    FrameBufferImageDefinition& definition = imageDefinitions[i];
    if (definition.type == FrameBufferImageDefinition::SwapChain)
      continue;
    // FIXME : framebuffer size should not be dependent on swap chain size
    ImageTraits imageTraits(definition.usage, definition.format, { surface->swapChainSize.width, surface->swapChainSize.height, 1 }, false, 1, 1,
      definition.samples, VK_IMAGE_LAYOUT_UNDEFINED, definition.aspectMask, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE,
      VK_IMAGE_VIEW_TYPE_2D, definition.swizzles);
    pddit->second.frameBufferImages[i] = std::make_unique<Image>(deviceSh, imageTraits);
  }
}

void FrameBufferImages::reset(std::shared_ptr<Surface> surface)
{
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit != perSurfaceData.end())
  {
    pddit->second.frameBufferImages.clear();
    perSurfaceData.erase(surface->surface);
  }
}


Image* FrameBufferImages::getImage(std::shared_ptr<Surface> surface, uint32_t imageIndex)
{
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
    if (d.type == FrameBufferImageDefinition::SwapChain)
      return d;
  return FrameBufferImageDefinition(FrameBufferImageDefinition::SwapChain, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT);
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
      vkDestroyFramebuffer(it.second.surface->device.lock()->device, f, nullptr);
  }
}

void FrameBuffer::reset(std::shared_ptr<Surface> surface)
{
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit != perSurfaceData.end())
  {
    for (auto f : pddit->second.frameBuffers)
      vkDestroyFramebuffer(pddit->second.surface->device.lock()->device, f, nullptr);
    perSurfaceData.erase(surface->surface);
  }
}


void FrameBuffer::validate(std::shared_ptr<Surface> surface, const std::vector<std::unique_ptr<Image>>& swapChainImages)
{
  std::shared_ptr<RenderPass> rp         = renderPass.lock();
  std::shared_ptr<Device> deviceSh       = surface->device.lock();
  std::shared_ptr<FrameBufferImages> fbi = frameBufferImages.lock();

  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(surface, swapChainImages.empty()? 1 : swapChainImages.size() ) }).first;
  if (!pddit->second.dirty)
    return;

  // create frame buffer images ( render pass attachments ), skip images marked as swap chain images ( as they're created already )
//  frameBufferImages.resize(rp->attachments.size());
  std::vector<VkImageView> imageViews;
  imageViews.resize(rp->attachments.size());
  for (uint32_t i = 0; i < rp->attachments.size(); i++)
  {
    AttachmentDefinition& definition          = rp->attachments[i];
    FrameBufferImageDefinition& fbiDefinition = fbi->imageDefinitions[definition.imageDefinitionIndex];
    if (fbiDefinition.type == FrameBufferImageDefinition::SwapChain)
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
      auto commandBuffer = deviceSh->beginSingleTimeCommands(surface->commandPool);
      commandBuffer->setImageLayout(*(fbi->getImage(surface, definition.imageDefinitionIndex)), fbiDefinition.aspectMask, VK_IMAGE_LAYOUT_UNDEFINED, definition.initialLayout);
      deviceSh->endSingleTimeCommands(commandBuffer, surface->presentationQueue);
    }
  }

  // create frame buffer for each swap chain image
//  frameBuffers.resize(swapChainImages.size());
  for (uint32_t i = 0; i < pddit->second.frameBuffers.size(); i++)
  {
    for (uint32_t j = 0; j < rp->attachments.size(); j++)
    {
      AttachmentDefinition& definition          = rp->attachments[j];
      FrameBufferImageDefinition& fbiDefinition = fbi->imageDefinitions[definition.imageDefinitionIndex];
      if (fbiDefinition.type == FrameBufferImageDefinition::SwapChain)
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
}

VkFramebuffer FrameBuffer::getFrameBuffer(std::shared_ptr<Surface> surface, uint32_t fbIndex)
{
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

void InputAttachment::validate(std::shared_ptr<Surface> surface)
{
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(surface) }).first;
  if (!pddit->second.dirty)
    return;
  pddit->second.dirty = false;
}

void InputAttachment::getDescriptorSetValues(VkSurfaceKHR surface, std::vector<DescriptorSetValue>& values) const
{
  auto pddit = perSurfaceData.find(surface);
  if (pddit == perSurfaceData.end())
    return;
  std::shared_ptr<Surface> s     = pddit->second.surface.lock();
  if (frameBuffer.get() != nullptr)
  {
    std::shared_ptr<RenderPass> rp         = frameBuffer->renderPass.lock();
    std::shared_ptr<FrameBufferImages> fbi = frameBuffer->frameBufferImages.lock();
    uint32_t actualIndex = rp->attachments[frameBufferIndex].imageDefinitionIndex;
    values.push_back(DescriptorSetValue(VK_NULL_HANDLE, fbi->getImage(s, frameBufferIndex)->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
  }
  else
  {
    std::shared_ptr<RenderPass> rp         = s->frameBuffer->renderPass.lock();
    std::shared_ptr<FrameBufferImages> fbi = s->frameBuffer->frameBufferImages.lock();
    uint32_t actualIndex = rp->attachments[frameBufferIndex].imageDefinitionIndex;
    values.push_back(DescriptorSetValue(VK_NULL_HANDLE, fbi->getImage(s, frameBufferIndex)->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
  }
}




}