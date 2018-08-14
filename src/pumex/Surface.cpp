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

#include <pumex/Surface.h>
#include <tbb/tbb.h>
#include <pumex/Viewer.h>
#include <pumex/Window.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/RenderPass.h>
#include <pumex/RenderVisitors.h>
#include <pumex/FrameBuffer.h>
#include <pumex/MemoryImage.h>
#include <pumex/Image.h>
#include <pumex/utils/Log.h>
#include <pumex/RenderWorkflow.h>
#include <pumex/TimeStatistics.h>

using namespace pumex;

SurfaceTraits::SurfaceTraits(uint32_t ic, VkColorSpaceKHR ics, uint32_t ial, VkPresentModeKHR  spm, VkSurfaceTransformFlagBitsKHR pt, VkCompositeAlphaFlagBitsKHR ca)
  : imageCount{ ic }, imageColorSpace{ ics }, imageArrayLayers{ ial }, swapchainPresentMode{ spm }, preTransform{ pt }, compositeAlpha{ ca }
{
}

const std::unordered_map<std::string, VkPresentModeKHR> Surface::nameToPresentationModes
{
  { "immediate",    VK_PRESENT_MODE_IMMEDIATE_KHR },
  { "mailbox",      VK_PRESENT_MODE_MAILBOX_KHR },
  { "fifo",         VK_PRESENT_MODE_FIFO_KHR },
  { "fifo_relaxed", VK_PRESENT_MODE_FIFO_RELAXED_KHR }
};

const std::unordered_map<VkPresentModeKHR, std::string> Surface::presentationModeNames
{
  { VK_PRESENT_MODE_IMMEDIATE_KHR,    "immediate" },
  { VK_PRESENT_MODE_MAILBOX_KHR,      "mailbox" },
  { VK_PRESENT_MODE_FIFO_KHR,         "fifo" },
  { VK_PRESENT_MODE_FIFO_RELAXED_KHR, "fifo_relaxed" }
};

const std::unordered_map<VkPresentModeKHR, std::vector<VkPresentModeKHR>> Surface::replacementModes
{
  { VK_PRESENT_MODE_IMMEDIATE_KHR,{ VK_PRESENT_MODE_MAILBOX_KHR , VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR } },
  { VK_PRESENT_MODE_MAILBOX_KHR,{ VK_PRESENT_MODE_IMMEDIATE_KHR , VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR } },
  { VK_PRESENT_MODE_FIFO_KHR,{ VK_PRESENT_MODE_FIFO_RELAXED_KHR , VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR } },
  { VK_PRESENT_MODE_FIFO_RELAXED_KHR,{ VK_PRESENT_MODE_FIFO_KHR , VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR } }
};


Surface::Surface(std::shared_ptr<Viewer> v, std::shared_ptr<Window> w, std::shared_ptr<Device> d, VkSurfaceKHR s, const SurfaceTraits& st)
  : viewer{ v }, window{ w }, device{ d }, surface{ s }, surfaceTraits(st)
{
  timeStatistics = std::make_unique<TimeStatistics>(32);

  timeStatistics->registerGroup(TSS_GROUP_BASIC,             L"Surface operations");
  timeStatistics->registerGroup(TSS_GROUP_EVENTS,            L"Surface events");
  timeStatistics->registerGroup(TSS_GROUP_SECONDARY_BUFFERS, L"Secondary buffers");

  timeStatistics->registerChannel(TSS_CHANNEL_BEGINFRAME,                   TSS_GROUP_BASIC,             L"beginFrame",                   glm::vec4(0.4f, 0.4f, 0.4f, 0.5f));
  timeStatistics->registerChannel(TSS_CHANNEL_EVENTSURFACERENDERSTART,      TSS_GROUP_EVENTS,            L"eventSurfaceRenderStart",      glm::vec4(0.8f, 0.8f, 0.1f, 0.5f));
  timeStatistics->registerChannel(TSS_CHANNEL_VALIDATEWORKFLOW,             TSS_GROUP_BASIC,             L"validateWorkflow",             glm::vec4(0.1f, 0.1f, 0.1f, 0.5f));
  timeStatistics->registerChannel(TSS_CHANNEL_VALIDATESECONDARYNODES,       TSS_GROUP_SECONDARY_BUFFERS, L"validateSecondaryNodes",       glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
  timeStatistics->registerChannel(TSS_CHANNEL_VALIDATESECONDARYDESCRIPTORS, TSS_GROUP_SECONDARY_BUFFERS, L"validateSecondaryDescriptors", glm::vec4(1.0f, 1.0f, 0.0f, 0.5f));
  timeStatistics->registerChannel(TSS_CHANNEL_BUILDSECONDARYCOMMANDBUFFERS, TSS_GROUP_SECONDARY_BUFFERS, L"buildSecondaryCommandBuffers", glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
  timeStatistics->registerChannel(TSS_CHANNEL_DRAW,                         TSS_GROUP_BASIC,             L"draw",                         glm::vec4(0.9f, 0.9f, 0.9f, 0.5f));
  timeStatistics->registerChannel(TSS_CHANNEL_ENDFRAME,                     TSS_GROUP_BASIC,             L"endFrame",                     glm::vec4(0.1f, 0.1f, 0.1f, 0.5f));
  timeStatistics->registerChannel(TSS_CHANNEL_EVENTSURFACERENDERFINISH,     TSS_GROUP_EVENTS,            L"eventSurfaceRenderFinish",     glm::vec4(0.8f, 0.8f, 0.1f, 0.5f));

  timeStatistics->setFlags(TSS_STAT_BASIC | TSS_STAT_BUFFERS | TSS_STAT_EVENTS);
}

Surface::~Surface()
{
  cleanup();
}

void Surface::realize()
{
  if (isRealized())
    return;

  auto deviceSh          = device.lock();
  VkPhysicalDevice phDev = deviceSh->physical.lock()->physicalDevice;
  VkDevice vkDevice      = deviceSh->device;

  // collect surface properties
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phDev, surface, &surfaceCapabilities), "failed vkGetPhysicalDeviceSurfaceCapabilitiesKHR for surface " << getID() );

  // collect available presentation modes
  uint32_t presentModeCount;
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfacePresentModesKHR(phDev, surface, &presentModeCount, nullptr), "Could not get present modes for surface " << getID());
  CHECK_LOG_THROW( presentModeCount == 0, "No present modes defined for this surface" );
  presentModes.resize(presentModeCount);
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfacePresentModesKHR(phDev, surface, &presentModeCount, presentModes.data()), "Could not get present modes " << presentModeCount << " for surface " << getID());

  // check if presentation mode from surface traits is available
  auto presentIt = std::find(begin(presentModes), end(presentModes), surfaceTraits.swapchainPresentMode);
  if (presentIt == end(presentModes))
  {
    // presentation mode from surface traits is not available. Choose the most appropriate one and inform user about the change

    auto prefIt = replacementModes.find(surfaceTraits.swapchainPresentMode);
    CHECK_LOG_THROW(prefIt == end(replacementModes), "Presentation mode <" <<surfaceTraits.swapchainPresentMode << "> not available on GPU and not recognized by library");
    VkPresentModeKHR finalPresentationMode = surfaceTraits.swapchainPresentMode;
    for (auto it = begin(prefIt->second); it != end(prefIt->second); ++it)
    {
      auto secondChoiceIt = std::find(begin(presentModes), end(presentModes), *it);
      if (secondChoiceIt == end(presentModes))
        continue;
      finalPresentationMode = *it;
      break;
    }
    CHECK_LOG_THROW(finalPresentationMode == surfaceTraits.swapchainPresentMode, "Presentation mode <" << surfaceTraits.swapchainPresentMode << "> not available on GPU. Library cannot find the replacement");

    LOG_WARNING << "Warning : <" << presentationModeNames.at(surfaceTraits.swapchainPresentMode) <<"> presentation mode  not available. Library will use <" << presentationModeNames.at(finalPresentationMode) << "> presentation mode instead." << std::endl ;
    surfaceTraits.swapchainPresentMode = finalPresentationMode;
  }

  uint32_t surfaceFormatCount;
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfaceFormatsKHR(phDev, surface, &surfaceFormatCount, nullptr), "Could not get surface formats for surface " << getID());
  CHECK_LOG_THROW(surfaceFormatCount == 0, "No surface formats defined for surface " << getID());
  surfaceFormats.resize(surfaceFormatCount);
  VK_CHECK_LOG_THROW( vkGetPhysicalDeviceSurfaceFormatsKHR(phDev, surface, &surfaceFormatCount, surfaceFormats.data()), "Could not get surface formats " << surfaceFormatCount << " for surface " << getID());

  uint32_t queueFamilyCount = deviceSh->physical.lock()->queueFamilyProperties.size();
  supportsPresent.resize(queueFamilyCount);
  for (uint32_t i = 0; i < queueFamilyCount; i++)
    VK_CHECK_LOG_THROW(vkGetPhysicalDeviceSurfaceSupportKHR(phDev, i, surface, &supportsPresent[i]), "failed vkGetPhysicalDeviceSurfaceSupportKHR for family " << i );

  CHECK_LOG_THROW(renderWorkflow.get() == nullptr, "Render workflow not defined for surface " << getID());
  CHECK_LOG_THROW(renderWorkflowCompiler.get() == nullptr, "Render workflow compiler not defined for surface " << getID());
  checkWorkflow();
  // Create synchronization objects
  VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  // get all queues and create command pools and command buffers for them
  for (auto& q : workflowResults->queueTraits)
  {
    std::shared_ptr<Queue> queue = deviceSh->getQueue(q, true);
    CHECK_LOG_THROW(queue.get() == nullptr, "Cannot get the queue for this surface");
    CHECK_LOG_THROW(supportsPresent[queue->familyIndex] == VK_FALSE, "Support not present for(device,surface,familyIndex) : " << queue->familyIndex);
    queues.push_back(queue);

    auto commandPool = std::make_shared<CommandPool>(queue->familyIndex);
    commandPool->validate(deviceSh.get());
    commandPools.push_back(commandPool);

    auto commandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, deviceSh.get(), commandPool, surfaceTraits.imageCount);
    primaryCommandBuffers.push_back(commandBuffer);

    // Create a semaphore used to synchronize command submission
    // Ensures that the image is not presented until all commands have been sumbitted and executed
    VkSemaphore semaphore0;
    VK_CHECK_LOG_THROW(vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &semaphore0), "Could not create render complete semaphore");
    frameBufferReadySemaphores.emplace_back(semaphore0);

    VkSemaphore semaphore1;
    VK_CHECK_LOG_THROW(vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &semaphore1), "Could not create render complete semaphore");
    renderCompleteSemaphores.emplace_back(semaphore1);
  }
  // define basic command buffers required to render a frame
  prepareCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, deviceSh.get(), commandPools[workflowResults->presentationQueueIndex], surfaceTraits.imageCount);
  presentCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, deviceSh.get(), commandPools[workflowResults->presentationQueueIndex], surfaceTraits.imageCount);

  // create all semaphores required to render a frame
  VK_CHECK_LOG_THROW( vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphore), "Could not create image available semaphore");
  VK_CHECK_LOG_THROW( vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &renderFinishedSemaphore), "Could not create image available semaphore");

  VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  waitFences.resize(surfaceTraits.imageCount);
  for (auto& fence : waitFences)
    VK_CHECK_LOG_THROW(vkCreateFence(vkDevice, &fenceCreateInfo, nullptr, &fence), "Could not create a surface wait fence");

  realized = true;
}

void Surface::cleanup()
{
  VkDevice dev = device.lock()->device;
  eventSurfaceRenderStart  = nullptr;
  eventSurfaceRenderFinish = nullptr;
  if (swapChain != VK_NULL_HANDLE)
  {
    swapChainImages.clear();
    vkDestroySwapchainKHR(dev, swapChain, nullptr);
    swapChain = VK_NULL_HANDLE;
  }
  if (surface != VK_NULL_HANDLE)
  {
    if (workflowResults != nullptr)
    {
      for( auto& frameBuffer : workflowResults->frameBuffers)
        frameBuffer->reset(this);
    }

    for (auto& fence : waitFences)
      vkDestroyFence(dev, fence, nullptr);

    for (auto sem : renderCompleteSemaphores)
      vkDestroySemaphore(dev, sem, nullptr);
    for (auto sem : frameBufferReadySemaphores)
      vkDestroySemaphore(dev, sem, nullptr);
    if(renderFinishedSemaphore != VK_NULL_HANDLE)
      vkDestroySemaphore(dev, renderFinishedSemaphore, nullptr);
    if (imageAvailableSemaphore != VK_NULL_HANDLE)
      vkDestroySemaphore(dev, imageAvailableSemaphore, nullptr);
    primaryCommandBuffers.clear();
    presentCommandBuffer = nullptr;
    prepareCommandBuffer = nullptr;
    commandPools.clear();
    for(auto q : queues )
      device.lock()->releaseQueue(q);
    queues.clear();
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

  VkSwapchainKHR oldSwapChain = swapChain;

  VK_CHECK_LOG_THROW(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phDev, surface, &surfaceCapabilities), "failed vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  swapChainSize = surfaceCapabilities.currentExtent;
//  LOG_ERROR << "cs " << swapChainSize.width << "x" << swapChainSize.height << std::endl;

  FrameBufferImageDefinition swapChainDefinition = workflowResults->getSwapChainImageDefinition();

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
    swapChainImages.clear();
    vkDestroySwapchainKHR(vkDevice, oldSwapChain, nullptr);
  }

  // collect new swap chain images
  uint32_t imageCount;
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, nullptr), "Could not get swapchain images");
  std::vector<VkImage> images(imageCount);
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, images.data()), "Could not get swapchain images " << imageCount);
  VkExtent3D extent{ swapChainSize.width, swapChainSize.height, 1 };
  for (uint32_t i = 0; i < imageCount; i++)
    swapChainImages.push_back(std::make_shared<Image>(deviceSh.get(), images[i], swapChainDefinition.format, extent, 1, 1));

  prepareCommandBuffer->invalidate(std::numeric_limits<uint32_t>::max());
  presentCommandBuffer->invalidate(std::numeric_limits<uint32_t>::max());
}

bool Surface::checkWorkflow()
{
  auto deviceSh = device.lock();
  renderWorkflow->compile(renderWorkflowCompiler);
  if (workflowResults.get() != renderWorkflow->workflowResults.get())
  {
    if (workflowResults != nullptr)
    {
      for (uint32_t i = 0; i < workflowResults->queueTraits.size(); ++i)
      {
        timeStatistics->unregisterChannels(TSS_GROUP_PRIMARY_BUFFERS + i);
        timeStatistics->unregisterGroup(TSS_GROUP_PRIMARY_BUFFERS + i);
      }
    }

    workflowResults = renderWorkflow->workflowResults;

    for (uint32_t i = 0; i < workflowResults->queueTraits.size(); ++i)
    {
      std::wstringstream ostr;
      ostr << " (" << i << ")";
      timeStatistics->registerGroup(TSS_GROUP_PRIMARY_BUFFERS + i, L"Primary buffers" + ostr.str());

      timeStatistics->registerChannel(20 + 10 * i + 0, TSS_GROUP_PRIMARY_BUFFERS + i, L"validatePrimaryNodes" + ostr.str(),       glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
      timeStatistics->registerChannel(20 + 10 * i + 1, TSS_GROUP_PRIMARY_BUFFERS + i, L"validatePrimaryDescriptors" + ostr.str(), glm::vec4(1.0f, 1.0f, 0.0f, 0.5f));
      timeStatistics->registerChannel(20 + 10 * i + 2, TSS_GROUP_PRIMARY_BUFFERS + i, L"buildPrimaryCommandBuffer" + ostr.str(),  glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
    }


    // invalidate basic command buffers
    if (prepareCommandBuffer.get() != nullptr)
      prepareCommandBuffer->invalidate(std::numeric_limits<uint32_t>::max());
    if(presentCommandBuffer.get() != nullptr)
      presentCommandBuffer->invalidate(std::numeric_limits<uint32_t>::max());
    for (auto& pcb : primaryCommandBuffers)
      pcb->invalidate(std::numeric_limits<uint32_t>::max());
    return true;
  }
  return false;
}

void Surface::beginFrame()
{
  resized = false;
  actions.performActions();
  auto deviceSh = device.lock();

  if (swapChain == VK_NULL_HANDLE)
  {
    createSwapChain();
    resized = true;
  }

  VkResult result = vkAcquireNextImageKHR(deviceSh->device, swapChain, UINT64_MAX, imageAvailableSemaphore, (VkFence)nullptr, &swapChainImageIndex);
  if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
  {
    // recreate swapchain
    createSwapChain();
    resized = true;
    // try to acquire images again - throw error for every reason other than VK_SUCCESS
    result = vkAcquireNextImageKHR(deviceSh->device, swapChain, UINT64_MAX, imageAvailableSemaphore, (VkFence)nullptr, &swapChainImageIndex);
  }
  VK_CHECK_LOG_THROW(result, "failed vkAcquireNextImageKHR");

  VK_CHECK_LOG_THROW(vkWaitForFences(deviceSh->device, 1, &waitFences[swapChainImageIndex], VK_TRUE, UINT64_MAX), "failed to wait for fence");
  VK_CHECK_LOG_THROW(vkResetFences(deviceSh->device, 1, &waitFences[swapChainImageIndex]), "failed to reset a fence");
}

void Surface::validateWorkflow()
{
  RenderContext renderContext(this, workflowResults->presentationQueueIndex);
  if (checkWorkflow() || resized)
  {
    for (auto& frameBuffer : workflowResults->frameBuffers)
    {
      frameBuffer->prepareMemoryImages(renderContext, swapChainImages);
      frameBuffer->invalidate(renderContext);
    }
  }
  for (auto& frameBuffer : workflowResults->frameBuffers)
    frameBuffer->validate(renderContext);

  // create/update render passes and compute passes for current surface
  for (auto& command : workflowResults->commands[workflowResults->presentationQueueIndex])
    command->validate(renderContext);

  // at the beginning of render we must transform frame buffer images into appropriate image layouts
  prepareCommandBuffer->setActiveIndex(swapChainImageIndex);
  if (!prepareCommandBuffer->isValid())
  {
    prepareCommandBuffer->cmdBegin();
    std::vector<PipelineBarrier> prepareBarriers;
    VkPipelineStageFlags dstStageFlags = 0;
    for ( const auto& iLayout : workflowResults->initialImageLayouts )
    {
      VkImageLayout  imageLayout;
      AttachmentType attachmentType;
      VkImageAspectFlags aspectMask;
      std::tie(imageLayout, attachmentType, aspectMask) = iLayout.second;

      if (imageLayout == VK_IMAGE_LAYOUT_UNDEFINED )
        continue;

      VkImageLayout oldLayout;
      VkAccessFlags srcAccessFlags,dstAccessFlags;
      switch (attachmentType)
      {
      case atSurface:
        srcAccessFlags = 0;
        dstAccessFlags = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        oldLayout      = VK_IMAGE_LAYOUT_UNDEFINED;// VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        dstStageFlags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        break;
      case atColor:
        srcAccessFlags = 0;
        dstAccessFlags = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        oldLayout      = VK_IMAGE_LAYOUT_UNDEFINED;
        dstStageFlags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        break;
      case atDepth:
      case atDepthStencil:
      case atStencil:
        srcAccessFlags = 0;
        dstAccessFlags = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        oldLayout      = VK_IMAGE_LAYOUT_UNDEFINED;
        dstStageFlags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        break;
      default:
        srcAccessFlags = 0;
        dstAccessFlags = 0;
        oldLayout      = VK_IMAGE_LAYOUT_UNDEFINED;
        dstStageFlags |= 0;
        break;
      }
      auto it = workflowResults->registeredImageViews.find(iLayout.first);
      if (it != end(workflowResults->registeredImageViews))
      {
        VkImage image = it->second->getHandleImage(renderContext);
        prepareBarriers.emplace_back(PipelineBarrier
        (
          srcAccessFlags,
          dstAccessFlags,
          VK_QUEUE_FAMILY_IGNORED,
          VK_QUEUE_FAMILY_IGNORED,
          image,
          it->second->memoryImage->getFullImageRange().getSubresource(),
          oldLayout,
          imageLayout
        ));
      }
    }
    if(!prepareBarriers.empty())
      prepareCommandBuffer->cmdPipelineBarrier(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, dstStageFlags, VK_DEPENDENCY_BY_REGION_BIT, prepareBarriers);
    prepareCommandBuffer->cmdEnd();
  }

  presentCommandBuffer->setActiveIndex(swapChainImageIndex);
  if (!presentCommandBuffer->isValid())
  {
    presentCommandBuffer->cmdBegin();
    PipelineBarrier presentBarrier
    (
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_ACCESS_MEMORY_READ_BIT,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      swapChainImages[swapChainImageIndex]->getHandleImage(),
      { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS },
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );
    presentCommandBuffer->cmdPipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_DEPENDENCY_BY_REGION_BIT, presentBarrier);
    presentCommandBuffer->cmdEnd();
  }
}

void Surface::setCommandBufferIndices()
{
  for (uint32_t i = 0; i < primaryCommandBuffers.size(); ++i)
    primaryCommandBuffers[i]->setActiveIndex(swapChainImageIndex);

  RenderContext renderContext(this, workflowResults->presentationQueueIndex);
  for (uint32_t i = 0; i < secondaryCommandBufferNodes.size(); ++i)
  {
    auto commandBuffer = secondaryCommandBufferNodes[i]->getSecondaryBuffer(renderContext);
    CHECK_LOG_THROW(commandBuffer == nullptr, "Secondary buffer not defined for node " << secondaryCommandBufferNodes[i]->getName());
    commandBuffer->setActiveIndex(swapChainImageIndex);
  }
}

void Surface::validatePrimaryNodes(uint32_t queueNumber)
{
  RenderContext renderContext(this, workflowResults->presentationQueueIndex);
  ValidateNodeVisitor validateNodeVisitor(renderContext, true);
  for (auto& command : workflowResults->commands[queueNumber])
    command->applyRenderContextVisitor(validateNodeVisitor);
}

void Surface::validatePrimaryDescriptors(uint32_t queueNumber)
{
  RenderContext renderContext(this, workflowResults->presentationQueueIndex);
  ValidateDescriptorVisitor validateDescriptorVisitor(renderContext, true);
  for (auto& command : workflowResults->commands[queueNumber])
    command->applyRenderContextVisitor(validateDescriptorVisitor);
}

void Surface::buildPrimaryCommandBuffer(uint32_t queueNumber)
{
  RenderContext renderContext(this, workflowResults->presentationQueueIndex);
  primaryCommandBuffers[queueNumber]->setActiveIndex(swapChainImageIndex);
  if (!primaryCommandBuffers[queueNumber]->isValid())
  {
    BuildCommandBufferVisitor cbVisitor(renderContext, primaryCommandBuffers[queueNumber].get(), true);

    primaryCommandBuffers[queueNumber]->cmdBegin();

    for (auto& command : workflowResults->commands[queueNumber])
      command->buildCommandBuffer(cbVisitor);

    primaryCommandBuffers[queueNumber]->cmdEnd();
  }
}

void Surface::validateSecondaryNodes()
{
  // find all secondary buffer nodes and place its data in a Surface owned vector ( is it thread friendly ?)
  RenderContext renderContext(this, workflowResults->presentationQueueIndex);
  FindSecondaryCommandBuffersVisitor fscbVisitor(renderContext);
  for (uint32_t i = 0; i < workflowResults->commands.size(); ++i)
    for (auto& command : workflowResults->commands[i])
      command->applyRenderContextVisitor(fscbVisitor);

  secondaryCommandBufferNodes        = fscbVisitor.nodes;
  secondaryCommandBufferRenderPasses = fscbVisitor.renderPasses;
  secondaryCommandBufferSubPasses    = fscbVisitor.subPasses;

  tbb::parallel_for
  (
    tbb::blocked_range<size_t>(0, secondaryCommandBufferNodes.size()),
      [&](const tbb::blocked_range<size_t>& r)
      {
        for (size_t i = r.begin(); i != r.end(); ++i)
        {
          RenderContext renderContext(this, workflowResults->presentationQueueIndex);
          renderContext.commandPool = secondaryCommandBufferNodes[i]->getSecondaryCommandPool(renderContext);
          ValidateNodeVisitor validateNodeVisitor(renderContext, false);
          secondaryCommandBufferNodes[i]->accept(validateNodeVisitor);
        }
      }
  );
}

void Surface::validateSecondaryDescriptors()
{
  tbb::parallel_for
  (
    tbb::blocked_range<size_t>(0, secondaryCommandBufferNodes.size()),
      [&](const tbb::blocked_range<size_t>& r)
      {
        for (size_t i = r.begin(); i != r.end(); ++i)
        {
          RenderContext renderContext(this, workflowResults->presentationQueueIndex);
          renderContext.commandPool = secondaryCommandBufferNodes[i]->getSecondaryCommandPool(renderContext);
          ValidateDescriptorVisitor validateDescriptorVisitor(renderContext, false);
          secondaryCommandBufferNodes[i]->accept(validateDescriptorVisitor);
        }
      }
  );
}

void Surface::buildSecondaryCommandBuffers()
{
  tbb::parallel_for
  (
    tbb::blocked_range<size_t>(0, secondaryCommandBufferNodes.size()),
      [&](const tbb::blocked_range<size_t>& r)
      {
        for (size_t i = r.begin(); i != r.end(); ++i)
        {
          RenderContext renderContext(this, workflowResults->presentationQueueIndex);
          auto commandBuffer = secondaryCommandBufferNodes[i]->getSecondaryBuffer(renderContext);
          CHECK_LOG_THROW(commandBuffer == nullptr, "Secondary buffer not defined for node " << secondaryCommandBufferNodes[i]->getName());
          commandBuffer->setActiveIndex(swapChainImageIndex);
          if (!commandBuffer->isValid())
          {
            // The problem is that above defined render context needs to use elements defined up the tree ( currentPipelineLayout, currentAssetBuffer and currentRenderMask )
            // We have to find that data
            CompleteRenderContextVisitor crcVisitor(renderContext);
            secondaryCommandBufferNodes[i]->accept(crcVisitor);

            // Now we are ready to build secondary command buffer
            BuildCommandBufferVisitor cbVisitor(renderContext, commandBuffer.get(), false);
            VkCommandBufferUsageFlags cbUsageFlags = 0;
            if (secondaryCommandBufferNodes[i]->getNumParents() > 1)
              cbUsageFlags |= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            if (secondaryCommandBufferRenderPasses[i] != VK_NULL_HANDLE)
              cbUsageFlags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
            commandBuffer->cmdBegin(cbUsageFlags, secondaryCommandBufferRenderPasses[i], secondaryCommandBufferSubPasses[i]);
            secondaryCommandBufferNodes[i]->accept(cbVisitor);
            commandBuffer->cmdEnd();
          }
        }
      }
  );
}

void Surface::draw()
{
  prepareCommandBuffer->queueSubmit(queues[workflowResults->presentationQueueIndex]->queue, { imageAvailableSemaphore }, { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }, frameBufferReadySemaphores, VK_NULL_HANDLE );

  for (uint32_t i = 0; i < queues.size(); ++i)
  {
    // submit command buffer to each queue with a semaphore signaling ent of work (renderCompleteSemaphores[i])
    primaryCommandBuffers[i]->queueSubmit(queues[i]->queue, { frameBufferReadySemaphores[i] }, { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }, { renderCompleteSemaphores[i] }, VK_NULL_HANDLE);
  }
}

void Surface::endFrame()
{
  // wait for all queues to finish work ( using renderCompleteSemaphores ), then submit command buffer converting output image to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR layout
  std::vector<VkPipelineStageFlags> waitStages;
  waitStages.resize(renderCompleteSemaphores.size(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  presentCommandBuffer->queueSubmit(queues[workflowResults->presentationQueueIndex]->queue, renderCompleteSemaphores, waitStages, { renderFinishedSemaphore }, waitFences[swapChainImageIndex]);

  // present output image when its layout is transformed into VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapChain;
    presentInfo.pImageIndices      = &swapChainImageIndex;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinishedSemaphore;
  VkResult result = vkQueuePresentKHR(queues[workflowResults->presentationQueueIndex]->queue, &presentInfo);

  if ((result != VK_ERROR_OUT_OF_DATE_KHR) && (result != VK_SUBOPTIMAL_KHR))
    VK_CHECK_LOG_THROW(result, "failed vkQueuePresentKHR");
}

void Surface::resizeSurface(uint32_t newWidth, uint32_t newHeight)
{
  if (!isRealized())
    return;
  if (swapChainSize.width != newWidth && swapChainSize.height != newHeight)
  {
    createSwapChain();
    resized = true;
  }
}

void Surface::setRenderWorkflow(std::shared_ptr<RenderWorkflow> workflow, std::shared_ptr<RenderWorkflowCompiler> compiler)
{
  renderWorkflow         = workflow;
  renderWorkflowCompiler = compiler;
}

std::shared_ptr<MemoryBuffer> Surface::getRegisteredMemoryBuffer(const std::string& name)
{
  CHECK_LOG_THROW(workflowResults == nullptr, "workflow not compiled");
  auto it = workflowResults->registeredMemoryBuffers.find(name);
  if (it != end(workflowResults->registeredMemoryBuffers))
    return it->second;
  return nullptr;
}

std::shared_ptr<MemoryImage> Surface::getRegisteredMemoryImage(const std::string& name)
{
  CHECK_LOG_THROW(workflowResults == nullptr, "workflow not compiled");
  auto it = workflowResults->registeredMemoryImages.find(name);
  if (it != end(workflowResults->registeredMemoryImages))
    return it->second;
  return nullptr;
}

std::shared_ptr<ImageView> Surface::getRegisteredImageView(const std::string& name)
{
  CHECK_LOG_THROW(workflowResults == nullptr, "workflow not compiled");
  auto it = workflowResults->registeredImageViews.find(name);
  if (it != end(workflowResults->registeredImageViews))
    return it->second;
  return nullptr;
}

void Surface::onEventSurfaceRenderStart()
{
  if (eventSurfaceRenderStart != nullptr)
    eventSurfaceRenderStart(shared_from_this());
}
void Surface::onEventSurfaceRenderFinish()
{
  if (eventSurfaceRenderFinish != nullptr)
    eventSurfaceRenderFinish(shared_from_this());
}

void Surface::onEventSurfacePrepareStatistics(TimeStatistics* viewerStatistics)
{
  if (eventSurfacePrepareStatistics != nullptr)
    eventSurfacePrepareStatistics(this, viewerStatistics, timeStatistics.get());
}

std::shared_ptr<CommandPool> Surface::getPresentationCommandPool()
{
  return commandPools[workflowResults->presentationQueueIndex];
}

std::shared_ptr<Queue> Surface::getPresentationQueue()
{
  return queues[workflowResults->presentationQueueIndex];
}
