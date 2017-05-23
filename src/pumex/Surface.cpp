#include <pumex/Surface.h>
#include <pumex/Viewer.h>
#include <pumex/Window.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Command.h>
#include <pumex/RenderPass.h>
#include <pumex/utils/Log.h>
#include <pumex/Texture.h>
#include <vulkan/vulkan.h>

using namespace pumex;

SurfaceTraits::SurfaceTraits(uint32_t ic, VkFormat ifo, VkColorSpaceKHR ics, uint32_t ial, VkFormat df, VkPresentModeKHR  spm, VkSurfaceTransformFlagBitsKHR pt, VkCompositeAlphaFlagBitsKHR ca)
  : imageCount{ ic }, imageFormat{ ifo }, imageColorSpace{ ics }, imageArrayLayers{ ial }, depthFormat{ df }, swapchainPresentMode{ spm }, preTransform{ pt }, compositeAlpha{ ca }, presentationQueueTraits{ VK_QUEUE_GRAPHICS_BIT, 0, { 0.5f } }
{
}

void SurfaceTraits::setDefaultRenderPass(std::shared_ptr<pumex::RenderPass> renderPass)
{
  defaultRenderPass = renderPass;
}

void SurfaceTraits::definePresentationQueue(const pumex::QueueTraits& queueTraits)
{
  presentationQueueTraits = queueTraits;
}

Surface::Surface(std::shared_ptr<pumex::Viewer> v, std::shared_ptr<pumex::Window> w, std::shared_ptr<pumex::Device> d, VkSurfaceKHR s, const pumex::SurfaceTraits& st)
  : viewer{ v }, window{ w }, device{ d }, surface{ s }, surfaceTraits(st)
{
  auto deviceSh = device.lock();
  VkPhysicalDevice phDev = deviceSh->physical.lock()->physicalDevice;
  VkDevice vkDevice = deviceSh->device;

  // collect surface properties
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phDev, surface, &surfaceCapabilities), "failed vkGetPhysicalDeviceSurfaceCapabilitiesKHR" );
  uint32_t presentModeCount;
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfacePresentModesKHR(phDev, surface, &presentModeCount, nullptr), "Could not get present modes" );
  CHECK_LOG_THROW( presentModeCount == 0, "No present modes defined for this surface" );
  presentModes.resize(presentModeCount);
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfacePresentModesKHR(phDev, surface, &presentModeCount, presentModes.data()), "Could not get present modes " << presentModeCount);

  uint32_t surfaceFormatCount;
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfaceFormatsKHR(phDev, surface, &surfaceFormatCount, nullptr), "Could not get surface formats");
  CHECK_LOG_THROW(surfaceFormatCount == 0, "No surface formats defined for this surface");
  surfaceFormats.resize(surfaceFormatCount);
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfaceFormatsKHR(phDev, surface, &surfaceFormatCount, surfaceFormats.data()), "Could not get surface formats " << surfaceFormatCount);

  uint32_t queueFamilyCount = deviceSh->physical.lock()->queueFamilyProperties.size();
  supportsPresent.resize(queueFamilyCount);
  for (uint32_t i = 0; i < queueFamilyCount; i++)
    VK_CHECK_LOG_THROW(vkGetPhysicalDeviceSurfaceSupportKHR(phDev, i, surface, &supportsPresent[i]), "failed vkGetPhysicalDeviceSurfaceSupportKHR for family " << i );

  // get the main queue
  presentationQueue = deviceSh->getQueue(surfaceTraits.presentationQueueTraits, true);
  CHECK_LOG_THROW( presentationQueue == VK_NULL_HANDLE, "Cannot get the presentation queue for this surface" );
  CHECK_LOG_THROW( (!deviceSh->getQueueIndices(presentationQueue, std::tie(presentationQueueFamilyIndex, presentationQueueIndex))), "Could not get data for (device, surface, familyIndex, index)" );
  CHECK_LOG_THROW(supportsPresent[presentationQueueFamilyIndex] == VK_FALSE, "Support not present for(device,surface,familyIndex) : " << presentationQueueFamilyIndex);

  // create command pool
  commandPool = std::make_shared<pumex::CommandPool>(presentationQueueFamilyIndex);
  commandPool->validate(deviceSh);

  // Create synchronization objects
  VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  // Create a semaphore used to synchronize image presentation
  // Ensures that the image is displayed before we start submitting new commands to the queue
  VK_CHECK_LOG_THROW( vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphore), "Could not create image available semaphore" );

  // Create a semaphore used to synchronize command submission
  // Ensures that the image is not presented until all commands have been sumbitted and executed
  VK_CHECK_LOG_THROW(vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &renderCompleteSemaphore), "Could not create render complete semaphore");

  // FIXME - do some checking maybe ?

  // define default render pass
  defaultRenderPass = surfaceTraits.defaultRenderPass;
  defaultRenderPass->validate(deviceSh);

  // define presentation command buffers
  for (uint32_t i = 0; i < surfaceTraits.imageCount; ++i)
  {
    prePresentCmdBuffers.push_back(std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, deviceSh,commandPool));
    postPresentCmdBuffers.push_back(std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, deviceSh,commandPool));
  }

  VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  waitFences.resize(surfaceTraits.imageCount);
  for (auto& fence : waitFences)
    VK_CHECK_LOG_THROW(vkCreateFence(vkDevice, &fenceCreateInfo, nullptr, &fence), "Could not create a surface wait fence");

  // create swapchain
  resizeSurface(window.lock()->width, window.lock()->height);
}

Surface::~Surface()
{
  cleanup();
}

void Surface::cleanup()
{
  VkDevice dev = device.lock()->device;
  if (swapChain != VK_NULL_HANDLE)
  {
    for (auto f : frameBuffers)
      vkDestroyFramebuffer(dev, f, nullptr);
    frameBufferImages.clear();
    swapChainImages.clear();
    vkDestroySwapchainKHR(dev, swapChain, nullptr);
    swapChain = VK_NULL_HANDLE;
  }
  if (surface != VK_NULL_HANDLE)
  {
    for (auto& fence : waitFences)
      vkDestroyFence(dev, fence, nullptr);
    postPresentCmdBuffers.clear();
    prePresentCmdBuffers.clear();
    defaultRenderPass = nullptr;
    vkDestroySemaphore(dev, renderCompleteSemaphore, nullptr);
    vkDestroySemaphore(dev, imageAvailableSemaphore, nullptr);
    commandPool = nullptr;
    device.lock()->releaseQueue(presentationQueue);
    presentationQueue = VK_NULL_HANDLE;
    vkDestroySurfaceKHR(viewer.lock()->getInstance(), surface, nullptr);
    surface = VK_NULL_HANDLE;
  }
}

void Surface::createSwapChain()
{
  auto deviceSh = device.lock();
  VkDevice vkDevice = deviceSh->device;
  VkPhysicalDevice phDev = deviceSh->physical.lock()->physicalDevice;

  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phDev, surface, &surfaceCapabilities), "failed vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  VkSwapchainKHR oldSwapChain = swapChain;

  VkSwapchainCreateInfoKHR swapchainCreateInfo{};
    swapchainCreateInfo.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface               = surface;
    swapchainCreateInfo.minImageCount         = surfaceTraits.imageCount;
    swapchainCreateInfo.imageFormat           = surfaceTraits.imageFormat;
    swapchainCreateInfo.imageColorSpace       = surfaceTraits.imageColorSpace;
    swapchainCreateInfo.imageExtent           = swapChainSize;
    swapchainCreateInfo.imageArrayLayers      = surfaceTraits.imageArrayLayers;
    swapchainCreateInfo.imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.queueFamilyIndexCount = 0;
    swapchainCreateInfo.pQueueFamilyIndices   = nullptr;
    swapchainCreateInfo.preTransform          = surfaceTraits.preTransform;
    swapchainCreateInfo.compositeAlpha        = surfaceTraits.compositeAlpha;
    swapchainCreateInfo.presentMode           = surfaceTraits.swapchainPresentMode;
    swapchainCreateInfo.clipped               = VK_TRUE;
    swapchainCreateInfo.oldSwapchain          = oldSwapChain;
  VK_CHECK_LOG_THROW( vkCreateSwapchainKHR(vkDevice, &swapchainCreateInfo, nullptr, &swapChain), "Could not create swapchain" );

  // remove old swap chain and all images
  if (oldSwapChain != VK_NULL_HANDLE)
  {
    for (auto f : frameBuffers)
      vkDestroyFramebuffer(vkDevice, f, nullptr);
    frameBufferImages.clear();
    swapChainImages.clear();
    vkDestroySwapchainKHR(vkDevice, oldSwapChain, nullptr);
  }

  // collect new swap chain images
  uint32_t imageCount;
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, nullptr), "Could not get swapchain images");
  std::vector<VkImage> images(imageCount);
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, images.data()), "Could not get swapchain images " << imageCount);
  for (uint32_t i = 0; i < imageCount; i++)
    swapChainImages.push_back(std::make_unique<Image>(deviceSh, images[i], surfaceTraits.imageFormat, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA)));

  // create frame buffer images ( render pass attachments ), skip images marked as swap chain images ( as they're created already )
  frameBufferImages.resize(defaultRenderPass->attachments.size());
  std::vector<VkImageView> attachments;
  attachments.resize(defaultRenderPass->attachments.size());
  for (uint32_t i = 0; i < defaultRenderPass->attachments.size(); i++)
  {
    AttachmentDefinition& definition = defaultRenderPass->attachments[i];
    if (definition.type == AttachmentDefinition::SwapChain)
      continue;
    ImageTraits imageTraits(definition.usage, definition.format, { swapChainSize.width, swapChainSize.height, 1 }, false, 1, 1,
      definition.samples, VK_IMAGE_LAYOUT_UNDEFINED, definition.aspectMask, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE,
      VK_IMAGE_VIEW_TYPE_2D, gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA));
    frameBufferImages[i] = std::make_unique<Image>(deviceSh, imageTraits);

    if (definition.initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
    {
      auto commandBuffer = deviceSh->beginSingleTimeCommands(commandPool);
      commandBuffer->setImageLayout(*(frameBufferImages[i].get()), definition.aspectMask, VK_IMAGE_LAYOUT_UNDEFINED, definition.initialLayout);
      deviceSh->endSingleTimeCommands(commandBuffer, presentationQueue);
    }
    attachments[i] = frameBufferImages[i]->getImageView();
  }
    
  // create frame buffer for each swap chain image
  frameBuffers.resize(swapChainImages.size());
  for (uint32_t i = 0; i < frameBuffers.size(); i++)
  {
    for (uint32_t j = 0; j < defaultRenderPass->attachments.size(); j++)
      if (defaultRenderPass->attachments[j].type == AttachmentDefinition::SwapChain)
        attachments[j] = swapChainImages[i]->getImageView();

    // define frame buffers
    VkFramebufferCreateInfo frameBufferCreateInfo{};
      frameBufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      frameBufferCreateInfo.renderPass      = defaultRenderPass->getHandle(vkDevice);
      frameBufferCreateInfo.attachmentCount = attachments.size();
      frameBufferCreateInfo.pAttachments    = attachments.data();
      frameBufferCreateInfo.width           = swapChainSize.width;
      frameBufferCreateInfo.height          = swapChainSize.height;
      frameBufferCreateInfo.layers          = 1;
    VK_CHECK_LOG_THROW(vkCreateFramebuffer(vkDevice, &frameBufferCreateInfo, nullptr, &frameBuffers[i]), "Could not create swapchain frame buffer" << i);
  }

  // define pre- and postpresentation command buffers
  postPresentCmdBuffers.resize(swapChainImages.size());
  prePresentCmdBuffers.resize(swapChainImages.size());
  for (uint32_t i = 0; i < swapChainImages.size(); ++i)
  {
    postPresentCmdBuffers[i]->cmdBegin();
    PipelineBarrier postPresentBarrier(0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, swapChainImages[i]->getImage(), { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } );
    postPresentCmdBuffers[i]->cmdPipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, postPresentBarrier);
    postPresentCmdBuffers[i]->cmdEnd();

    prePresentCmdBuffers[i]->cmdBegin();
    PipelineBarrier prePresentBarrier(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, swapChainImages[i]->getImage(), { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    prePresentCmdBuffers[i]->cmdPipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, prePresentBarrier);
    prePresentCmdBuffers[i]->cmdEnd();
  }

}

void Surface::beginFrame()
{
  actions.performActions();
  // FIXME : VK_SUBOPTIMAL_KHR
  auto deviceSh = device.lock();

  VK_CHECK_LOG_THROW(vkAcquireNextImageKHR(deviceSh->device, swapChain, UINT64_MAX, imageAvailableSemaphore, (VkFence)nullptr, &swapChainImageIndex), "failed vkAcquireNextImageKHR" );
  VK_CHECK_LOG_THROW(vkWaitForFences(deviceSh->device, 1, &waitFences[swapChainImageIndex], VK_TRUE, UINT64_MAX), "failed to wait for fence");
  VK_CHECK_LOG_THROW(vkResetFences(deviceSh->device, 1, &waitFences[swapChainImageIndex]), "failed to reset a fence");
  postPresentCmdBuffers[swapChainImageIndex]->queueSubmit(presentationQueue);
}

void Surface::endFrame()
{
  auto deviceSh = device.lock();
  // Submit pre present image barrier to transform the image from color attachment to present(khr) for presenting to the swap chain
  prePresentCmdBuffers[swapChainImageIndex]->queueSubmit(presentationQueue, {}, {}, {}, waitFences[swapChainImageIndex]);

  // FIXME - isn't a place for synchronizing many windows at once ?
  // In that case we shouldn't call it for single surface, I suppose...
  VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapChain;
    presentInfo.pImageIndices      = &swapChainImageIndex;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderCompleteSemaphore;
  VK_CHECK_LOG_THROW(vkQueuePresentKHR(presentationQueue, &presentInfo), "failed vkQueuePresentKHR");
//  VK_CHECK_LOG_THROW(vkQueueWaitIdle(presentationQueue), "failed vkQueueWaitIdle")
}

void Surface::resizeSurface(uint32_t newWidth, uint32_t newHeight)
{
  swapChainSize = VkExtent2D{ newWidth, newHeight };
  createSwapChain();
}

InputAttachment::InputAttachment(uint32_t fbi)
  : frameBufferIndex{ fbi }
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
  values.push_back(DescriptorSetValue(VK_NULL_HANDLE, s->frameBufferImages[frameBufferIndex]->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
//  values.push_back(DescriptorSetValue(VK_NULL_HANDLE, s->frameBufferImages[frameBufferIndex]->getImageView(), s->frameBufferImages[frameBufferIndex]->getImageLayout()));
  
}


