#include <pumex/Surface.h>
#include <pumex/Viewer.h>
#include <pumex/Window.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Command.h>
#include <pumex/RenderPass.h>
#include <pumex/FrameBuffer.h>
#include <pumex/utils/Log.h>
#include <pumex/Texture.h>
#include <vulkan/vulkan.h>

using namespace pumex;

SurfaceTraits::SurfaceTraits(uint32_t ic, VkColorSpaceKHR ics, uint32_t ial, VkPresentModeKHR  spm, VkSurfaceTransformFlagBitsKHR pt, VkCompositeAlphaFlagBitsKHR ca)
  : imageCount{ ic }, imageColorSpace{ ics }, imageArrayLayers{ ial }, swapchainPresentMode{ spm }, preTransform{ pt }, compositeAlpha{ ca }, presentationQueueTraits{ VK_QUEUE_GRAPHICS_BIT, 0, { 0.5f } }
{
}

void SurfaceTraits::setDefaultRenderPass(std::shared_ptr<pumex::RenderPass> rp)
{
  defaultRenderPass = rp;
}

void SurfaceTraits::setFrameBufferImages(std::shared_ptr<pumex::FrameBufferImages> fbi)
{
  frameBufferImages = fbi;
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
  auto pp = std::tie(presentationQueueFamilyIndex, presentationQueueIndex);
  CHECK_LOG_THROW( (!deviceSh->getQueueIndices(presentationQueue, pp)), "Could not get data for (device, surface, familyIndex, index)" );
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

  // define default render pass
  defaultRenderPass = surfaceTraits.defaultRenderPass;
  frameBufferImages = surfaceTraits.frameBufferImages;

  defaultRenderPass->validate(deviceSh);

  frameBuffer = std::make_unique<FrameBuffer>(defaultRenderPass, frameBufferImages);

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
    frameBuffer->reset(shared_from_this());
    frameBufferImages->reset(shared_from_this());
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

  vkDeviceWaitIdle(vkDevice);
  
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phDev, surface, &surfaceCapabilities), "failed vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  VkSwapchainKHR oldSwapChain = swapChain;

  FrameBufferImageDefinition swapChainDefinition = frameBufferImages->getSwapChainDefinition();

  VkSwapchainCreateInfoKHR swapchainCreateInfo{};
    swapchainCreateInfo.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface               = surface;
    swapchainCreateInfo.minImageCount         = surfaceTraits.imageCount;
    swapchainCreateInfo.imageFormat           = swapChainDefinition.format;
    swapchainCreateInfo.imageColorSpace       = surfaceTraits.imageColorSpace;
    swapchainCreateInfo.imageExtent           = swapChainSize;
    swapchainCreateInfo.imageArrayLayers      = surfaceTraits.imageArrayLayers;
    swapchainCreateInfo.imageUsage            = swapChainDefinition.usage;
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
    frameBuffer->reset(shared_from_this());
    frameBufferImages->reset(shared_from_this());
    swapChainImages.clear();
    vkDestroySwapchainKHR(vkDevice, oldSwapChain, nullptr);
  }

  // collect new swap chain images
  uint32_t imageCount;
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, nullptr), "Could not get swapchain images");
  std::vector<VkImage> images(imageCount);
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, images.data()), "Could not get swapchain images " << imageCount);
  for (uint32_t i = 0; i < imageCount; i++)
    swapChainImages.push_back(std::make_unique<Image>(deviceSh, images[i], swapChainDefinition.format, 1, 1, swapChainDefinition.aspectMask, VK_IMAGE_VIEW_TYPE_2D, swapChainDefinition.swizzles));

  frameBufferImages->validate(shared_from_this());
  frameBuffer->validate(shared_from_this(), swapChainImages);

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
}

void Surface::resizeSurface(uint32_t newWidth, uint32_t newHeight)
{
  swapChainSize = VkExtent2D{ newWidth, newHeight };
  createSwapChain();
}

VkFramebuffer Surface::getCurrentFrameBuffer() 
{ 
  return frameBuffer->getFrameBuffer(shared_from_this(), swapChainImageIndex); 
}




