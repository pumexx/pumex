#include <pumex/FrameBuffer.h>
#include <pumex/Texture.h>
#include <pumex/Surface.h>
#include <pumex/Command.h>
#include <pumex/utils/Log.h>

namespace pumex
{

FrameBuffer::FrameBuffer(std::shared_ptr<RenderPass> rp)
  : renderPass{ rp }
{
}

FrameBuffer::~FrameBuffer()
{
  for (auto it : perSurfaceData)
  {
    for (auto f : it.second.frameBuffers)
      vkDestroyFramebuffer(it.second.device, f, nullptr);
    it.second.frameBufferImages.clear();
  }
}

void FrameBuffer::reset(std::shared_ptr<Surface> surface)
{
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit != perSurfaceData.end())
  {
    for (auto f : pddit->second.frameBuffers)
      vkDestroyFramebuffer(pddit->second.device, f, nullptr);
    pddit->second.frameBufferImages.clear();
    perSurfaceData.erase(surface->surface);
  }
}


void FrameBuffer::validate(std::shared_ptr<Surface> surface, const std::vector<std::unique_ptr<Image>>& swapChainImages)
{
  std::shared_ptr<RenderPass> rp   = renderPass.lock();
  std::shared_ptr<Device> deviceSh = surface->device.lock();

  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(swapChainImages.empty()? 1 : swapChainImages.size(), rp->attachments.size(), surface->device.lock()->device) }).first;
  if (!pddit->second.dirty)
    return;

  // create frame buffer images ( render pass attachments ), skip images marked as swap chain images ( as they're created already )
//  frameBufferImages.resize(rp->attachments.size());
  std::vector<VkImageView> attachments;
  attachments.resize(rp->attachments.size());
  for (uint32_t i = 0; i < rp->attachments.size(); i++)
  {
    AttachmentDefinition& definition = rp->attachments[i];
    if (definition.type == AttachmentDefinition::SwapChain)
    {
      attachments[i] = VK_NULL_HANDLE;
      continue;
    }
    ImageTraits imageTraits(definition.usage, definition.format, { surface->swapChainSize.width, surface->swapChainSize.height, 1 }, false, 1, 1,
      definition.samples, VK_IMAGE_LAYOUT_UNDEFINED, definition.aspectMask, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE,
      VK_IMAGE_VIEW_TYPE_2D, gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA));
    pddit->second.frameBufferImages[i] = std::make_unique<Image>(deviceSh, imageTraits);

    if (definition.initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
    {
      auto commandBuffer = deviceSh->beginSingleTimeCommands(surface->commandPool);
      commandBuffer->setImageLayout(*(pddit->second.frameBufferImages[i].get()), definition.aspectMask, VK_IMAGE_LAYOUT_UNDEFINED, definition.initialLayout);
      deviceSh->endSingleTimeCommands(commandBuffer, surface->presentationQueue);
    }
    attachments[i] = pddit->second.frameBufferImages[i]->getImageView();
  }

  // create frame buffer for each swap chain image
//  frameBuffers.resize(swapChainImages.size());
  for (uint32_t i = 0; i < pddit->second.frameBuffers.size(); i++)
  {
    for (uint32_t j = 0; j < rp->attachments.size(); j++)
      if (rp->attachments[j].type == AttachmentDefinition::SwapChain)
        attachments[j] = swapChainImages[i]->getImageView();

    // define frame buffers
    VkFramebufferCreateInfo frameBufferCreateInfo{};
      frameBufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      frameBufferCreateInfo.renderPass      = rp->getHandle(deviceSh->device);
      frameBufferCreateInfo.attachmentCount = attachments.size();
      frameBufferCreateInfo.pAttachments    = attachments.data();
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

Image* FrameBuffer::getImage(std::shared_ptr<Surface> surface, uint32_t imageIndex)
{
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    return nullptr;
  if (pddit->second.frameBufferImages.size() <= imageIndex)
    return nullptr;
  return pddit->second.frameBufferImages[imageIndex].get();
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
  std::shared_ptr<Surface> s = pddit->second.surface.lock();
  if(frameBuffer.get() != nullptr)
    values.push_back(DescriptorSetValue(VK_NULL_HANDLE, frameBuffer->getImage(s, frameBufferIndex)->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
  else
    values.push_back(DescriptorSetValue(VK_NULL_HANDLE, s->frameBuffer->getImage(s, frameBufferIndex)->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
}




}