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

#include <pumex/Surface.h>
#include <pumex/Viewer.h>
#include <pumex/Window.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Command.h>
#include <pumex/RenderPass.h>
#include <pumex/RenderVisitors.h>
#include <pumex/FrameBuffer.h>
#include <pumex/utils/Log.h>
#include <pumex/Texture.h>

using namespace pumex;

SurfaceTraits::SurfaceTraits(uint32_t ic, VkColorSpaceKHR ics, uint32_t ial, VkPresentModeKHR  spm, VkSurfaceTransformFlagBitsKHR pt, VkCompositeAlphaFlagBitsKHR ca)
  : imageCount{ ic }, imageColorSpace{ ics }, imageArrayLayers{ ial }, swapchainPresentMode{ spm }, preTransform{ pt }, compositeAlpha{ ca }
{
}

void SurfaceTraits::setRenderWorkflow(std::shared_ptr<RenderWorkflow> rw)
{
  rw->compile();
  renderWorkflow = rw;
}

Surface::Surface(std::shared_ptr<Viewer> v, std::shared_ptr<Window> w, std::shared_ptr<Device> d, VkSurfaceKHR s, const SurfaceTraits& st)
  : viewer{ v }, window{ w }, device{ d }, surface{ s }, surfaceTraits(st), renderWorkflow(st.renderWorkflow)
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
  presentationQueue = deviceSh->getQueue(renderWorkflow->getPresentationQueue(), true);
  CHECK_LOG_THROW( presentationQueue == VK_NULL_HANDLE, "Cannot get the presentation queue for this surface" );
  auto pp = std::tie(presentationQueueFamilyIndex, presentationQueueIndex);
  CHECK_LOG_THROW( (!deviceSh->getQueueIndices(presentationQueue, pp)), "Could not get data for (device, surface, familyIndex, index)" );
  CHECK_LOG_THROW(supportsPresent[presentationQueueFamilyIndex] == VK_FALSE, "Support not present for(device,surface,familyIndex) : " << presentationQueueFamilyIndex);

  // create command pool
  commandPool = std::make_shared<CommandPool>(presentationQueueFamilyIndex);
  commandPool->validate(deviceSh.get());

  // Create synchronization objects
  VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  // Create a semaphore used to synchronize image presentation
  // Ensures that the image is displayed before we start submitting new commands to the queue
  VK_CHECK_LOG_THROW( vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphore), "Could not create image available semaphore" );

  // Create a semaphore used to synchronize command submission
  // Ensures that the image is not presented until all commands have been sumbitted and executed
  VK_CHECK_LOG_THROW(vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &renderCompleteSemaphore), "Could not create render complete semaphore");

  // define presentation and primary command buffers
  presentCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, deviceSh.get(), commandPool.get(), surfaceTraits.imageCount);
  primaryCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, deviceSh.get(), commandPool.get(), surfaceTraits.imageCount);

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
    renderWorkflow->frameBuffer->reset(this);
    renderWorkflow->frameBufferImages->reset(this);
    swapChainImages.clear();
    vkDestroySwapchainKHR(dev, swapChain, nullptr);
    swapChain = VK_NULL_HANDLE;
  }
  if (surface != VK_NULL_HANDLE)
  {
    for (auto& fence : waitFences)
      vkDestroyFence(dev, fence, nullptr);
    presentCommandBuffer = nullptr;
    primaryCommandBuffer = nullptr;
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

  FrameBufferImageDefinition swapChainDefinition = renderWorkflow->frameBufferImages->getSwapChainDefinition();

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
    renderWorkflow->frameBuffer->reset(this);
    renderWorkflow->frameBufferImages->reset(this);
    swapChainImages.clear();
    vkDestroySwapchainKHR(vkDevice, oldSwapChain, nullptr);
  }

  // collect new swap chain images
  uint32_t imageCount;
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, nullptr), "Could not get swapchain images");
  std::vector<VkImage> images(imageCount);
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, images.data()), "Could not get swapchain images " << imageCount);
  for (uint32_t i = 0; i < imageCount; i++)
    swapChainImages.push_back(std::make_unique<Image>(deviceSh.get(), images[i], swapChainDefinition.format, 1, 1, swapChainDefinition.aspectMask, VK_IMAGE_VIEW_TYPE_2D, swapChainDefinition.swizzles));

  validateGPUData(false);
  renderWorkflow->frameBufferImages->validate(this);
  renderWorkflow->frameBuffer->validate(this, swapChainImages);

  // define prepresentation command buffers
  for (uint32_t i = 0; i < swapChainImages.size(); ++i)
  {
    // dummy image barrier
    presentCommandBuffer->setActiveIndex(i);
    presentCommandBuffer->cmdBegin();
    PipelineBarrier prePresentBarrier(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, swapChainImages[i]->getImage(), { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    presentCommandBuffer->cmdPipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, prePresentBarrier);
    presentCommandBuffer->cmdEnd();
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
}

void Surface::validateGPUData(bool validateRenderGraphs)
{
  ValidateGPUVisitor validateVisitor(this, validateRenderGraphs);
  for (auto commandSequence : renderWorkflow->commandSequences)
    for( auto command : commandSequence )
      command->validateGPUData(validateVisitor);
}

void Surface::buildPrimaryCommandBuffer()
{
  primaryCommandBuffer->setActiveIndex(swapChainImageIndex);
  if (!primaryCommandBuffer->isValid(swapChainImageIndex))
  {
    BuildCommandBufferVisitor cbVisitor(this, primaryCommandBuffer.get());

    primaryCommandBuffer->cmdBegin();

    for (auto commandSequence : renderWorkflow->commandSequences)
      for (auto command : commandSequence)
        command->buildCommandBuffer(cbVisitor);

    primaryCommandBuffer->cmdEnd();
  }
}


void Surface::draw()
{
  primaryCommandBuffer->setActiveIndex(swapChainImageIndex);
  primaryCommandBuffer->queueSubmit(presentationQueue, { imageAvailableSemaphore }, { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }, { renderCompleteSemaphore }, VK_NULL_HANDLE);
}


void Surface::endFrame()
{
  auto deviceSh = device.lock();
  // Submit pre present dummy image barrier so that we are able to signal a fence
  presentCommandBuffer->setActiveIndex(swapChainImageIndex);
  presentCommandBuffer->queueSubmit(presentationQueue, {}, {}, {}, waitFences[swapChainImageIndex]);

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
