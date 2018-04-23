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
#include <pumex/Surface.h>
#include <pumex/RenderPass.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/RenderContext.h>
#include <pumex/Image.h>
#include <pumex/Sampler.h>
#include <pumex/utils/Log.h>

namespace pumex
{

FrameBufferImageDefinition::FrameBufferImageDefinition(AttachmentType at, VkFormat f, VkImageUsageFlags u, VkImageAspectFlags am, VkSampleCountFlagBits s, const std::string& n, const AttachmentSize& as, const gli::swizzles& sw)
  : attachmentType{ at }, format{ f }, usage{ u }, aspectMask{ am }, samples{ s }, name{ n }, attachmentSize { as }, swizzles{ sw }
{
}

FrameBufferImages::FrameBufferImages(const std::vector<FrameBufferImageDefinition>& fbid, std::shared_ptr<DeviceMemoryAllocator> a)
  : imageDefinitions(fbid), allocator{ a }
{
}

FrameBufferImages::~FrameBufferImages()
{
  for (auto it : perSurfaceData)
    it.second.frameBufferImages.clear();
}

void FrameBufferImages::invalidate(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perSurfaceData.find(surface->surface);
  if(it != end(perSurfaceData))
    it->second.valid = false;
}

void FrameBufferImages::validate(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == end(perSurfaceData))
    pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(surface->device.lock()->device,imageDefinitions.size()) }).first;
  if (pddit->second.valid)
    return;

  for (uint32_t i = 0; i < pddit->second.frameBufferImages.size(); ++i)
    pddit->second.frameBufferImages[i] = nullptr;

  for (uint32_t i = 0; i < imageDefinitions.size(); i++)
  {
    FrameBufferImageDefinition& definition = imageDefinitions[i];
    if (definition.attachmentType == atSurface)
      continue;
    VkExtent3D imSize;
    switch (definition.attachmentSize.attachmentSize)
    {
    case AttachmentSize::SurfaceDependent:
    {
      imSize.width  = surface->swapChainSize.width  * definition.attachmentSize.imageSize.x;
      imSize.height = surface->swapChainSize.height * definition.attachmentSize.imageSize.y;
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
    ImageTraits imageTraits(definition.usage, definition.format, imSize, 1, 1, definition.samples, 
      false, VK_IMAGE_LAYOUT_UNDEFINED, definition.aspectMask, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE,
      VK_IMAGE_VIEW_TYPE_2D, definition.swizzles);
    pddit->second.frameBufferImages[i] = std::make_shared<Image>(surface->device.lock().get(), imageTraits, allocator);
  }
  pddit->second.valid = true;
}

void FrameBufferImages::reset(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit != end(perSurfaceData))
  {
    pddit->second.frameBufferImages.clear();
    perSurfaceData.erase(surface->surface);
  }
}

Image* FrameBufferImages::getImage(Surface* surface, uint32_t imageIndex)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == end(perSurfaceData))
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
  return FrameBufferImageDefinition(atSurface, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, "swapchain");
}

FrameBuffer::FrameBuffer(std::shared_ptr<Surface> s, uint32_t count)
  : surface{ s }
{
  valid.resize(count, false);
  frameBuffers.resize(count, VK_NULL_HANDLE);
}

FrameBuffer::~FrameBuffer()
{
  reset();
}

void FrameBuffer::setFrameBufferImages(std::shared_ptr<FrameBufferImages> fbi)
{
  frameBufferImages = fbi;
  invalidate();
}

void FrameBuffer::setRenderPass(std::shared_ptr<RenderPass> rp)
{
  renderPass = rp;
  invalidate();
}

void FrameBuffer::reset()
{
  auto device = surface.lock()->device.lock();
  std::lock_guard<std::mutex> lock(mutex);
  for (auto f : frameBuffers)
    vkDestroyFramebuffer(device->device, f, nullptr);
}

void FrameBuffer::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  std::fill(begin(valid), end(valid), false);
  invalidateInputAttachments();
}


void FrameBuffer::validate(uint32_t index, const std::vector<std::unique_ptr<Image>>& swapChainImages)
{
  std::lock_guard<std::mutex> lock(mutex);

  uint32_t activeIndex = index % swapChainImages.size();
  if (valid[activeIndex])
    return;

  CHECK_LOG_THROW(renderPass.use_count() == 0, "FrameBuffer::validate() : render pass was not defined");
  std::shared_ptr<RenderPass> rp = renderPass.lock();
  std::shared_ptr<Surface> surf  = surface.lock();
  std::shared_ptr<Device> device = surf->device.lock();

  if (frameBuffers[activeIndex] != VK_NULL_HANDLE)
    vkDestroyFramebuffer(device->device, frameBuffers[activeIndex], nullptr);

  // create frame buffer images ( render pass attachments ), skip images marked as swap chain images ( as they're created already )
  std::vector<VkImageView> imageViews;
  imageViews.resize(rp->attachments.size());
  for (uint32_t i = 0; i < rp->attachments.size(); i++)
  {
    AttachmentDefinition& definition          = rp->attachments[i];
    FrameBufferImageDefinition& fbiDefinition = frameBufferImages->imageDefinitions[definition.imageDefinitionIndex];
    if (fbiDefinition.attachmentType == atSurface)
    {
      imageViews[i] = swapChainImages[activeIndex]->getImageView();
      continue;
    }
    else
    {
      imageViews[i] = frameBufferImages->getImage(surface.lock().get(), definition.imageDefinitionIndex)->getImageView();
    }
  }

  // define frame buffers
  VkFramebufferCreateInfo frameBufferCreateInfo{};
    frameBufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferCreateInfo.renderPass      = rp->getHandle(device->device);
    frameBufferCreateInfo.attachmentCount = imageViews.size();
    frameBufferCreateInfo.pAttachments    = imageViews.data();
    frameBufferCreateInfo.width           = surf->swapChainSize.width;
    frameBufferCreateInfo.height          = surf->swapChainSize.height;
    frameBufferCreateInfo.layers          = 1;
  VK_CHECK_LOG_THROW(vkCreateFramebuffer(device->device, &frameBufferCreateInfo, nullptr, &frameBuffers[activeIndex]), "Could not create frame buffer " << activeIndex);
  valid[activeIndex] = true;
  notifyCommandBuffers();
}

VkFramebuffer FrameBuffer::getFrameBuffer(uint32_t index)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (frameBuffers.size() <= index)
    return VK_NULL_HANDLE;
  return frameBuffers[index];
}

void FrameBuffer::addInputAttachment(const std::shared_ptr<InputAttachment> inputAttachment)
{
  if (std::find_if(begin(inputAttachments), end(inputAttachments), [&inputAttachment](std::weak_ptr<InputAttachment> ia) { return !ia.expired() && ia.lock().get() == inputAttachment.get(); }) == end(inputAttachments))
    inputAttachments.push_back(inputAttachment);
}

void FrameBuffer::invalidateInputAttachments()
{
  // remove expired attachments and invalidate remaining attachments
  auto eit = std::remove_if(begin(inputAttachments), end(inputAttachments), [](std::weak_ptr<InputAttachment> ia) { return ia.expired();  });
  for (auto it = begin(inputAttachments); it != eit; ++it)
    it->lock()->invalidate();
  inputAttachments.erase(eit, end(inputAttachments));
}

InputAttachment::InputAttachment(const std::string& an, std::shared_ptr<Sampler> s)
  : Resource{ Resource::OnceForAllSwapChainImages }, attachmentName{ an }, sampler{ s }
{
}

InputAttachment::~InputAttachment()
{
  sampler = nullptr;
}

std::pair<bool, VkDescriptorType> InputAttachment::getDefaultDescriptorType()
{
  return{ true, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT };
}

void InputAttachment::validate(const RenderContext& renderContext)
{
  if (sampler != nullptr)
    sampler->validate(renderContext);
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(renderContext.vkSurface);
  if (pddit == end(perSurfaceData))
    pddit = perSurfaceData.insert({ renderContext.vkSurface, PerSurfaceData() }).first;
  if (pddit->second.valid)
    return;
  pddit->second.valid = true;
}

void InputAttachment::invalidate()
{
  if (sampler != nullptr)
    sampler->invalidate();
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perSurfaceData)
    pdd.second.valid = false;
  invalidateDescriptors();
}

DescriptorSetValue InputAttachment::getDescriptorSetValue(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(renderContext.vkSurface);
  if (pddit == end(perSurfaceData))
    return DescriptorSetValue(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  auto frameBuffer = renderContext.surface->frameBuffer;
  uint32_t frameBufferIndex = UINT32_MAX;
  for (uint32_t i = 0; i < frameBuffer->getFrameBufferImages()->imageDefinitions.size(); ++i)
  {
    if (frameBuffer->getFrameBufferImages()->imageDefinitions[i].name == attachmentName)
    {
      frameBuffer->addInputAttachment(std::dynamic_pointer_cast<InputAttachment>(shared_from_this()));
      frameBufferIndex = i;
      break;
    }
  }
  CHECK_LOG_THROW(frameBufferIndex == UINT32_MAX, "Can't find input attachment with name : " << attachmentName);
  VkSampler samp = (sampler != nullptr) ? sampler->getHandleSampler(renderContext) : VK_NULL_HANDLE;
  return DescriptorSetValue(samp, frameBuffer->getFrameBufferImages()->getImage(renderContext.surface, frameBufferIndex)->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

}