#include <pumex/Surface.h>
#include <pumex/Viewer.h>
#include <pumex/Window.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/SurfaceThread.h>
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
    prePresentCmdBuffers.push_back(std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY,commandPool));
    postPresentCmdBuffers.push_back(std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, commandPool));
  }
  for (uint32_t i = 0; i < surfaceTraits.imageCount; ++i)
  {
    prePresentCmdBuffers[i]->validate(deviceSh);
    postPresentCmdBuffers[i]->validate(deviceSh);
  }

  // create swapchain
  resizeSurface(window.lock()->width, window.lock()->height);
}

Surface::~Surface()
{
  cleanup();
}

void Surface::setSurfaceThread(std::shared_ptr<pumex::SurfaceThread> s)
{
  surfaceThread = s;
}


void Surface::cleanup()
{
  VkDevice dev = device.lock()->device;
  if (swapChain != VK_NULL_HANDLE)
  {
    for (auto f : frameBuffers)
      vkDestroyFramebuffer(dev, f, nullptr);
    cleanupDepthStencil();
    for (auto s : swapChainImages)
      vkDestroyImageView(dev, s.view, nullptr);
    swapChainImages.clear();
    vkDestroySwapchainKHR(dev, swapChain, nullptr);
    swapChain = VK_NULL_HANDLE;
  }
  if (surface != VK_NULL_HANDLE)
  {
    postPresentCmdBuffers.resize(0);
    prePresentCmdBuffers.resize(0);
    defaultRenderPass = VK_NULL_HANDLE;
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

  if (oldSwapChain != VK_NULL_HANDLE)
  {
    for (auto f : frameBuffers)
      vkDestroyFramebuffer(vkDevice, f, nullptr);
    cleanupDepthStencil();
    for (auto s : swapChainImages)
      vkDestroyImageView(vkDevice, s.view, nullptr);
    vkDestroySwapchainKHR(vkDevice, oldSwapChain, nullptr);
  }

  uint32_t imageCount;
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, nullptr), "Could not get swapchain images");
  std::vector<VkImage> images(imageCount);
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, images.data()), "Could not get swapchain images " << imageCount);

  // Get the swap chain buffers containing the image and imageview
  swapChainImages.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; i++)
  {
    VkImageViewCreateInfo imageViewCreateInfo = {};
      imageViewCreateInfo.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      imageViewCreateInfo.format     = surfaceTraits.imageFormat;
      imageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
      imageViewCreateInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
      imageViewCreateInfo.subresourceRange.levelCount     = 1;
      imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
      imageViewCreateInfo.subresourceRange.layerCount     = 1;
      imageViewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
      imageViewCreateInfo.image = swapChainImages[i].image = images[i];
      swapChainImages[i].mem = VK_NULL_HANDLE;
      VK_CHECK_LOG_THROW(vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &swapChainImages[i].view), "Could not create swapchain image view" << i);
  }

  // FIXME - define depth buffer
  setupDepthStencil(swapChainSize.width, swapChainSize.height, 1, surfaceTraits.depthFormat);

  VkImageView attachments[2];
  attachments[1] = depthStencil.view;

  // define frame buffers
  VkFramebufferCreateInfo frameBufferCreateInfo{};
    frameBufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferCreateInfo.renderPass      = defaultRenderPass->getHandle(vkDevice);
    frameBufferCreateInfo.attachmentCount = 2;
    frameBufferCreateInfo.pAttachments    = attachments;
    frameBufferCreateInfo.width           = swapChainSize.width;
    frameBufferCreateInfo.height          = swapChainSize.height;
    frameBufferCreateInfo.layers          = 1;

  frameBuffers.resize(swapChainImages.size());
  for (uint32_t i = 0; i < frameBuffers.size(); i++)
  {
    attachments[0] = swapChainImages[i].view;
    VK_CHECK_LOG_THROW( vkCreateFramebuffer(vkDevice, &frameBufferCreateInfo, nullptr, &frameBuffers[i]), "Could not create swapchain frame buffer" << i);
  }

  // define presentation command buffers
  postPresentCmdBuffers.resize(swapChainImages.size());
  prePresentCmdBuffers.resize(swapChainImages.size());

  VkCommandBufferBeginInfo cmdBufInfo{};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  for (uint32_t i = 0; i < swapChainImages.size(); ++i)
  {
    
    //MemoryBarriers m = { {}, {}, { 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, swapChainImages[i].image } };
    //postPresentCmdBuffers->cmdPipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m);
    postPresentCmdBuffers[i]->cmdBegin(deviceSh);
    PipelineBarrier postPresentBarrier(0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, swapChainImages[i].image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } );
    postPresentCmdBuffers[i]->cmdPipelineBarrier(deviceSh, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, postPresentBarrier);
    //VkImageMemoryBarrier postPresentBarrier{};
    //  postPresentBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    //  postPresentBarrier.srcAccessMask       = 0;
    //  postPresentBarrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    //  postPresentBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    //  postPresentBarrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    //  postPresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    //  postPresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    //  postPresentBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    //  postPresentBarrier.image               = swapChainImages[i].image;
    //vkCmdPipelineBarrier(postPresentCmdBuffers[i], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    //  0, 0, nullptr, 0, nullptr, 1, &postPresentBarrier);
    postPresentCmdBuffers[i]->cmdEnd(deviceSh);

    prePresentCmdBuffers[i]->cmdBegin(deviceSh);
    PipelineBarrier prePresentBarrier(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, swapChainImages[i].image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    prePresentCmdBuffers[i]->cmdPipelineBarrier(deviceSh, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, prePresentBarrier);
    //VkImageMemoryBarrier prePresentBarrier{};
    //  prePresentBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    //  prePresentBarrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    //  prePresentBarrier.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
    //  prePresentBarrier.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    //  prePresentBarrier.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    //  prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    //  prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    //  prePresentBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    //  prePresentBarrier.image               = swapChainImages[i].image;
    //vkCmdPipelineBarrier( prePresentCmdBuffers[i], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
    //  0, 0, nullptr, 0, nullptr, 1, &prePresentBarrier);
    prePresentCmdBuffers[i]->cmdEnd(deviceSh);
  }

}

void Surface::setupDepthStencil(uint32_t width, uint32_t height, uint32_t depth, VkFormat depthFormat)
{
  auto deviceSh     = device.lock();
  VkDevice vkDevice = deviceSh->device;

  VkImageCreateInfo image = {};
    image.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType   = VK_IMAGE_TYPE_2D;
    image.format      = depthFormat;
    image.extent      = { width, height, 1 };
    image.mipLevels   = 1;
    image.arrayLayers = 1;
    image.samples     = VK_SAMPLE_COUNT_1_BIT;
    image.tiling      = VK_IMAGE_TILING_OPTIMAL;
    image.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

  VkMemoryAllocateInfo mem_alloc = {};
    mem_alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc.pNext           = nullptr;
    mem_alloc.allocationSize  = 0;
    mem_alloc.memoryTypeIndex = 0;

  VkImageViewCreateInfo depthStencilView = {};
    depthStencilView.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthStencilView.pNext    = nullptr;
    depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthStencilView.format   = depthFormat;
    depthStencilView.flags    = 0;
    depthStencilView.subresourceRange = {};
    depthStencilView.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    depthStencilView.subresourceRange.baseMipLevel   = 0;
    depthStencilView.subresourceRange.levelCount     = 1;
    depthStencilView.subresourceRange.baseArrayLayer = 0;
    depthStencilView.subresourceRange.layerCount     = 1;
  VK_CHECK_LOG_THROW(vkCreateImage(vkDevice, &image, nullptr, &depthStencil.image), "failed vkCreateImage");

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(vkDevice, depthStencil.image, &memReqs);
  mem_alloc.allocationSize  = memReqs.size;
  mem_alloc.memoryTypeIndex = deviceSh->physical.lock()->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK_LOG_THROW(vkAllocateMemory(vkDevice, &mem_alloc, nullptr, &depthStencil.mem), "failed vkAllocateMemory" << mem_alloc.allocationSize << " " << mem_alloc.memoryTypeIndex);

  VK_CHECK_LOG_THROW(vkBindImageMemory(vkDevice, depthStencil.image, depthStencil.mem, 0), "failed vkBindImageMemory");

  auto commandBuffer = deviceSh->beginSingleTimeCommands(commandPool);
  // FIXME - move setImageLayout to pumex::CommandBuffer
  pumex::setImageLayout(commandBuffer->getHandle(deviceSh->device), depthStencil.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  deviceSh->endSingleTimeCommands(commandBuffer, presentationQueue);
  depthStencilView.image = depthStencil.image;
  VK_CHECK_LOG_THROW(vkCreateImageView(vkDevice, &depthStencilView, nullptr, &depthStencil.view), "failed vkCreateImageView");
}

void Surface::cleanupDepthStencil()
{
  VkDevice dev = device.lock()->device;

  vkDestroyImageView(dev, depthStencil.view, nullptr);
  vkDestroyImage(dev, depthStencil.image, nullptr);
  vkFreeMemory(dev, depthStencil.mem, nullptr);
}

void Surface::beginFrame()
{
  // FIXME : VK_SUBOPTIMAL_KHR
  auto deviceSh = device.lock();
  VK_CHECK_LOG_THROW(vkAcquireNextImageKHR(deviceSh->device, swapChain, UINT64_MAX, imageAvailableSemaphore, (VkFence)nullptr, &swapChainImageIndex), "failed vkAcquireNextImageKHR" );

  // Submit post present image barrier to transform the image back to a color attachment that our render pass can write to
  postPresentCmdBuffers[swapChainImageIndex]->queueSubmit(deviceSh, presentationQueue);
}

void Surface::endFrame()
{
  auto deviceSh = device.lock();
  // Submit pre present image barrier to transform the image from color attachment to present(khr) for presenting to the swap chain
  prePresentCmdBuffers[swapChainImageIndex]->queueSubmit(deviceSh, presentationQueue);

  // FIXME - isn't a place for synchronizing many windows at once ?
  // In that case we shouldn't call it for single surface, I suppose...
  VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &swapChainImageIndex;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderCompleteSemaphore;
  VK_CHECK_LOG_THROW(vkQueuePresentKHR(presentationQueue, &presentInfo), "failed vkQueuePresentKHR");
  // FIXME : eliminate vkQueueWaitIdle ?
  VK_CHECK_LOG_THROW(vkQueueWaitIdle(presentationQueue), "failed vkQueueWaitIdle")
}

void Surface::resizeSurface(uint32_t newWidth, uint32_t newHeight)
{
  swapChainSize = VkExtent2D{ newWidth, newHeight };
  createSwapChain();
}

