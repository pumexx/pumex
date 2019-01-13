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
#include <pumex/RenderGraphExecution.h>
#include <pumex/MemoryImage.h>
#include <pumex/utils/Log.h>
#include <pumex/TimeStatistics.h>

using namespace pumex;

SurfaceTraits::SurfaceTraits(const ResourceDefinition& sdef, uint32_t sic, VkColorSpaceKHR sics, VkPresentModeKHR spm, VkSurfaceTransformFlagBitsKHR pt, VkCompositeAlphaFlagBitsKHR ca)
  : swapChainDefinition{ sdef }, minSwapChainImageCount{ sic }, swapChainImageColorSpace{ sics }, swapchainPresentMode{ spm }, preTransform{ pt }, compositeAlpha{ ca }
{
}

const std::unordered_map<std::string, VkPresentModeKHR> Surface::nameToPresentationModes
{
  { "immediate",    VK_PRESENT_MODE_IMMEDIATE_KHR },
  { "mailbox",      VK_PRESENT_MODE_MAILBOX_KHR },
  { "fifo",         VK_PRESENT_MODE_FIFO_KHR },
  { "fifo_relaxed", VK_PRESENT_MODE_FIFO_RELAXED_KHR }
};

const std::map<VkPresentModeKHR, std::string> Surface::presentationModeNames
{
  { VK_PRESENT_MODE_IMMEDIATE_KHR,    "immediate" },
  { VK_PRESENT_MODE_MAILBOX_KHR,      "mailbox" },
  { VK_PRESENT_MODE_FIFO_KHR,         "fifo" },
  { VK_PRESENT_MODE_FIFO_RELAXED_KHR, "fifo_relaxed" }
};

const std::map<VkPresentModeKHR, std::vector<VkPresentModeKHR>> Surface::replacementModes
{
  { VK_PRESENT_MODE_IMMEDIATE_KHR,{ VK_PRESENT_MODE_MAILBOX_KHR , VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR } },
  { VK_PRESENT_MODE_MAILBOX_KHR,{ VK_PRESENT_MODE_IMMEDIATE_KHR , VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR } },
  { VK_PRESENT_MODE_FIFO_KHR,{ VK_PRESENT_MODE_FIFO_RELAXED_KHR , VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR } },
  { VK_PRESENT_MODE_FIFO_RELAXED_KHR,{ VK_PRESENT_MODE_FIFO_KHR , VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR } }
};


Surface::Surface(std::shared_ptr<Device> d, std::shared_ptr<Window> w, VkSurfaceKHR s, const SurfaceTraits& st)
  : device{ d }, window{ w }, surface{ s }, surfaceTraits{ st }
{
  timeStatistics      = std::make_unique<TimeStatistics>(32);
  timeStatistics->registerGroup(TSS_GROUP_BASIC,             L"Surface operations");
  timeStatistics->registerGroup(TSS_GROUP_EVENTS,            L"Surface events");
  timeStatistics->registerGroup(TSS_GROUP_SECONDARY_BUFFERS, L"Secondary buffers");

  timeStatistics->registerChannel(TSS_CHANNEL_BEGINFRAME,                   TSS_GROUP_BASIC,             L"beginFrame",                   glm::vec4(0.4f, 0.4f, 0.4f, 0.5f));
  timeStatistics->registerChannel(TSS_CHANNEL_EVENTSURFACERENDERSTART,      TSS_GROUP_EVENTS,            L"eventSurfaceRenderStart",      glm::vec4(0.8f, 0.8f, 0.1f, 0.5f));
  timeStatistics->registerChannel(TSS_CHANNEL_VALIDATERENDERGRAPH,          TSS_GROUP_BASIC,             L"validateRenderGraphs",         glm::vec4(0.1f, 0.1f, 0.1f, 0.5f));
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

// called before realization - collects queue traits for all render graphs
void Surface::collectQueueTraits()
{
  auto v = viewer.lock();
  for (auto& rgData : renderGraphData)
  {
    auto currentSize = queueTraits.size();
    std::string& rgName = std::get<0>(rgData);
    auto qTraits = v->getRenderGraphQueueTraits(rgName);
    auto qit = renderGraphQueueIndices.insert({ rgName, std::vector<uint32_t>() }).first;
    for (uint32_t i=0; i<qTraits.size(); ++i)
    {
      switch(qTraits[i].assignment)
      {
      case qaExclusive:
      {
        qit->second.push_back(queueTraits.size());
        queueTraits.push_back(qTraits[i]);
        break;
      }
      case qaShared:
      {
        // Queues may be shared between different render graphs. Queue sharing inside single render graph makes no sense ( possible synchronization deadlocks, 
        // when two command sequences designed to work in parallel will be spawned serially )
        auto last = begin(queueTraits) + currentSize;
        auto it = std::find(begin(queueTraits), last , qTraits[i]);
        if (it == last)
        {
          qit->second.push_back(queueTraits.size());
          queueTraits.push_back(qTraits[i]);
        }
        else
          qit->second.push_back(std::distance(begin(queueTraits), it));
        break;
      }
      default:
        break;
      }
    }
  }
  auto d = device.lock();
  for( auto& qt : queueTraits )
    d->addRequestedQueue(qt);
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

  CHECK_LOG_THROW(renderGraphData.empty(), "There are no render graphs defined for surface " << getID());

  // get all queues and create command pools and entry semaphores for them
  for (uint32_t i=0; i<queueTraits.size(); ++i )
  {
    std::shared_ptr<Queue> queue = deviceSh->getQueue(queueTraits[i], true);
    CHECK_LOG_THROW(queue.get() == nullptr, "Cannot get the queue for this surface : " << i);
    CHECK_LOG_THROW(supportsPresent[queue->familyIndex] == VK_FALSE, "Support not present for(device,surface,familyIndex) : " << queue->familyIndex);
    queues.push_back(queue);

    // first queue able to perform graphics operations will become presentation queue
    if (presentationQueueIndex == -1 && ((queueTraits[i].mustHave & VK_QUEUE_GRAPHICS_BIT) != 0))
      presentationQueueIndex = i;

    auto commandPool = std::make_shared<CommandPool>(queue->familyIndex);
    commandPool->validate(deviceSh.get());
    commandPools.push_back(commandPool);

  }
  
  recreateSwapChain();
  resized = true;

  // Create synchronization objects
  VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  for (uint32_t i = 0; i < renderGraphData.size(); ++i)
  {

    // for all command sequences - figure out which queues they use, add command buffers for them
    auto renderGraphName = std::get<0>(renderGraphData[i]);
    auto queueIndices = renderGraphQueueIndices.find(renderGraphName);
    CHECK_LOG_THROW(queueIndices == end(renderGraphQueueIndices), "Missing renderGraphQueueIndices for render graph : " << renderGraphName);

    auto pcbit = primaryCommandBuffers.insert({ renderGraphName,std::vector<std::shared_ptr<CommandBuffer>>() }).first;
    auto qcbit = queueSubmissionCompletedSemaphores.insert({ renderGraphName, std::vector<VkSemaphore>() }).first;
    for (auto& queueIndex : queueIndices->second)
    {
      auto commandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, deviceSh.get(), commandPools[queueIndex], swapChainImageCount);
      pcbit->second.push_back(commandBuffer);

      VkSemaphore semaphore;
      VK_CHECK_LOG_THROW(vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &semaphore), "Could not create render complete semaphore");
      qcbit->second.push_back(semaphore);
    }
    // register time statistics for all render graphs
    for (uint32_t j = 0; j < queueIndices->second.size(); ++j)
    {
      if (timeStatistics->hasGroup(TSS_GROUP_PRIMARY_BUFFERS + i))
        continue;
      std::wstringstream ostr;
      std::wstring rgName;
      rgName.assign(begin(renderGraphName), end(renderGraphName));
      ostr << rgName << " (" << queueIndices->second[j] << ")";
      timeStatistics->registerGroup(TSS_GROUP_PRIMARY_BUFFERS + i, L"Primary buffers " + ostr.str());

      timeStatistics->registerChannel(20 + 10 * i + 0, TSS_GROUP_PRIMARY_BUFFERS + i, L"validatePrimaryNodes " + ostr.str(), glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
      timeStatistics->registerChannel(20 + 10 * i + 1, TSS_GROUP_PRIMARY_BUFFERS + i, L"validatePrimaryDescriptors " + ostr.str(), glm::vec4(1.0f, 1.0f, 0.0f, 0.5f));
      timeStatistics->registerChannel(20 + 10 * i + 2, TSS_GROUP_PRIMARY_BUFFERS + i, L"buildPrimaryCommandBuffer " + ostr.str(), glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
    }
  }

  // define basic command buffers required to render a frame
  presentCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, deviceSh.get(), commandPools[presentationQueueIndex], swapChainImageCount);

  // create all semaphores required to render a frame
  VK_CHECK_LOG_THROW( vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphore), "Could not create image available semaphore");
  VK_CHECK_LOG_THROW( vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, nullptr, &renderFinishedSemaphore), "Could not create image available semaphore");

  VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  waitFences.resize(swapChainImageCount);
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
  auto v = viewer.lock();
  if (surface != VK_NULL_HANDLE)
  {
    for (auto& rgData : renderGraphData)
    {
      auto executable = v->getRenderGraphExecutable(std::get<0>(rgData));
      if(executable == nullptr)
        continue;
      for (auto& frameBuffer : executable->frameBuffers)
        frameBuffer->reset(this);
    }
    renderGraphData.clear();
    for (auto& fence : waitFences)
      vkDestroyFence(dev, fence, nullptr);
    waitFences.clear();

    for (auto& semaphores : queueSubmissionCompletedSemaphores)
      for (auto sem : semaphores.second)
        vkDestroySemaphore(dev, sem, nullptr);
    queueSubmissionCompletedSemaphores.clear();
    if (renderFinishedSemaphore != VK_NULL_HANDLE)
    {
      vkDestroySemaphore(dev, renderFinishedSemaphore, nullptr);
      renderFinishedSemaphore = VK_NULL_HANDLE;
    }
    if (imageAvailableSemaphore != VK_NULL_HANDLE)
    {
      vkDestroySemaphore(dev, imageAvailableSemaphore, nullptr);
      imageAvailableSemaphore = VK_NULL_HANDLE;
    }
    primaryCommandBuffers.clear();
    presentCommandBuffer = nullptr;
    commandPools.clear();
    for(auto q : queues )
      device.lock()->releaseQueue(q);
    queues.clear();
    if(surfaceTraits.destroySurfaceOnCleanup)
      vkDestroySurfaceKHR(v->getInstance(), surface, nullptr);
    surface = VK_NULL_HANDLE;
  }
}

void Surface::recreateSwapChain()
{
  auto deviceSh = device.lock();
  VkDevice vkDevice = deviceSh->device;
  VkPhysicalDevice phDev = deviceSh->physical.lock()->physicalDevice;

  vkDeviceWaitIdle(vkDevice);

  VkSwapchainKHR oldSwapChain = swapChain;

  VK_CHECK_LOG_THROW(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phDev, surface, &surfaceCapabilities), "failed vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  swapChainSize = surfaceCapabilities.currentExtent;
//  LOG_ERROR << "cs " << swapChainSize.width << "x" << swapChainSize.height << std::endl;

  VkSwapchainCreateInfoKHR swapchainCreateInfo{};
    swapchainCreateInfo.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface               = surface;
    swapchainCreateInfo.minImageCount         = surfaceTraits.minSwapChainImageCount;
    swapchainCreateInfo.imageFormat           = surfaceTraits.swapChainDefinition.attachment.format;
    swapchainCreateInfo.imageColorSpace       = surfaceTraits.swapChainImageColorSpace;
    swapchainCreateInfo.imageExtent           = swapChainSize;
    swapchainCreateInfo.imageArrayLayers      = surfaceTraits.swapChainDefinition.attachment.attachmentSize.arrayLayers;
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
    swapChainImages.clear();
    vkDestroySwapchainKHR(vkDevice, oldSwapChain, nullptr);
  }

  // collect new swap chain images
  uint32_t newImageCount;
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &newImageCount, nullptr), "Could not get swapchain images");
  std::vector<VkImage> images(newImageCount);
  VK_CHECK_LOG_THROW(vkGetSwapchainImagesKHR(vkDevice, swapChain, &newImageCount, images.data()), "Could not get swapchain images " << newImageCount);
  for (uint32_t i = 0; i < newImageCount; i++)
    swapChainImages.push_back(std::make_shared<Image>(deviceSh.get(), images[i], surfaceTraits.swapChainDefinition.attachment.format, surfaceTraits.swapChainDefinition.attachment.attachmentSize));

  CHECK_LOG_THROW( swapChainImageCount != 0 && newImageCount != swapChainImageCount, "Cannot change swapChainImageCount while working" );
  swapChainImageCount = newImageCount;

  if(presentCommandBuffer != nullptr)
    presentCommandBuffer->invalidate(std::numeric_limits<uint32_t>::max());
}

void Surface::beginFrame()
{
  actions.performActions();
  auto deviceSh = device.lock();

  VkResult result = vkAcquireNextImageKHR(deviceSh->device, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &swapChainImageIndex);
  if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
  {
    // recreate swapchain
    recreateSwapChain();
    resized = true;
    // try to acquire images again - throw error for every reason other than VK_SUCCESS
    result = vkAcquireNextImageKHR(deviceSh->device, swapChain, UINT64_MAX, imageAvailableSemaphore, (VkFence)nullptr, &swapChainImageIndex);
  }
  VK_CHECK_LOG_THROW(result, "failed vkAcquireNextImageKHR");
  VK_CHECK_LOG_THROW(vkWaitForFences(deviceSh->device, 1, &waitFences[swapChainImageIndex], VK_TRUE, UINT64_MAX), "failed to wait for fence");
  VK_CHECK_LOG_THROW(vkResetFences(deviceSh->device, 1, &waitFences[swapChainImageIndex]), "failed to reset a fence");
}

void Surface::validateRenderGraphs()
{
  RenderContext renderContext(this, presentationQueueIndex);
  auto v = viewer.lock();
  if (resized)
  {
    for (auto& rgData : renderGraphData)
    {
      auto renderGraphName = std::get<0>(rgData);
      auto executable = v->getRenderGraphExecutable(renderGraphName);
      if(executable == nullptr)
        continue;
      executable->resizeImages(renderContext, swapChainImages);
      for (auto& frameBuffer : executable->frameBuffers)
      {
        auto rp = frameBuffer->getRenderPass().lock();
        rp->invalidate(renderContext);
        frameBuffer->invalidate(renderContext);
      }
    }
    resized = false;
  }

  for (auto& rgData : renderGraphData)
  {
    auto renderGraphName = std::get<0>(rgData);
    auto executable = v->getRenderGraphExecutable(renderGraphName);
    if(executable == nullptr)
      continue;
    renderContext.setRenderGraphExecutable(executable);
    for (auto& frameBuffer : executable->frameBuffers)
      frameBuffer->validate(renderContext);
    // create/update render passes and compute passes for current surface
    for (auto& commandSeq : executable->commands)
      for (auto& command : commandSeq)
        command->validate(renderContext);
  }

  // find last layout used by swapchain
  VkImageLayout swapChainImageFinalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  for (auto& rgData : renderGraphData)
  {
    auto renderGraphName = std::get<0>(rgData);
    auto executable = v->getRenderGraphExecutable(renderGraphName);
    if(executable == nullptr)
      continue;
    for (auto& memImage : executable->memoryImages)
    {
      auto ait = executable->imageInfo.find(memImage.first);
      if (ait == end(executable->imageInfo))
        continue;
      // if it's swapchain image - find last layout that is not VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR nor VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
      if (ait->second.isSwapchainImage)
      {
        // FIXME : We are looking only for one mipmap and one layer. Is it OK ?
        auto allLayouts = executable->getImageLayouts(memImage.first, ImageSubresourceRange());
        auto lit = std::find_if(rbegin(allLayouts), rend(allLayouts), [](const VkImageLayout& layout) { return layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && layout != VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR; });
        if (lit != rend(allLayouts))
          swapChainImageFinalLayout = *lit;
      }
    }
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
      swapChainImageFinalLayout,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );
    presentCommandBuffer->cmdPipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_DEPENDENCY_BY_REGION_BIT, presentBarrier);
    presentCommandBuffer->cmdEnd();
  }
}

void Surface::setCommandBufferIndices()
{
  for (auto& pcb : primaryCommandBuffers)
    for (auto& pcbx : pcb.second)
      pcbx->setActiveIndex(swapChainImageIndex);

  RenderContext renderContext(this, presentationQueueIndex);
  for (uint32_t i = 0; i < secondaryCommandBufferNodes.size(); ++i)
  {
    auto commandBuffer = secondaryCommandBufferNodes[i]->getSecondaryBuffer(renderContext);
    CHECK_LOG_THROW(commandBuffer == nullptr, "Secondary buffer not defined for node " << secondaryCommandBufferNodes[i]->getName());
    commandBuffer->setActiveIndex(swapChainImageIndex);
  }
}

void Surface::validatePrimaryNodes(uint32_t queueIndex)
{
  ValidateNodeVisitor validateNodeVisitor(RenderContext(this, presentationQueueIndex), true);
  auto v = viewer.lock();
  for (auto& rgData : renderGraphData)
  {
    auto renderGraphName = std::get<0>(rgData);
    auto executable = v->getRenderGraphExecutable(renderGraphName);
    if(executable == nullptr)
      continue;
    validateNodeVisitor.renderContext.setRenderGraphExecutable(executable);
    auto qit = renderGraphQueueIndices.find(renderGraphName);
    for (uint32_t i=0; i<qit->second.size(); ++i)
    {
      if (qit->second[i] == queueIndex)
      {
        for (auto& command : executable->commands[i])
          command->applyRenderContextVisitor(validateNodeVisitor);
      }
    }
  }
}

void Surface::validatePrimaryDescriptors(uint32_t queueIndex)
{
  ValidateDescriptorVisitor validateDescriptorVisitor(RenderContext(this, presentationQueueIndex), true);
  auto v = viewer.lock();
  for (auto& rgData : renderGraphData)
  {
    auto renderGraphName = std::get<0>(rgData);
    auto executable = v->getRenderGraphExecutable(renderGraphName);
    if(executable == nullptr)
      continue;
    validateDescriptorVisitor.renderContext.setRenderGraphExecutable(executable);
    auto qit = renderGraphQueueIndices.find(renderGraphName);
    for (uint32_t i = 0; i<qit->second.size(); ++i)
    {
      if (qit->second[i] == queueIndex)
      {
        for (auto& command : executable->commands[i])
          command->applyRenderContextVisitor(validateDescriptorVisitor);
      }
    }
  }
}

void Surface::buildPrimaryCommandBuffer(uint32_t queueIndex)
{
  RenderContext renderContext(this, presentationQueueIndex);
  auto v = viewer.lock();
  for (auto& rgData : renderGraphData)
  {
    auto renderGraphName = std::get<0>(rgData);
    auto executable      = v->getRenderGraphExecutable(renderGraphName);
    if(executable == nullptr)
      continue;
    auto qit             = renderGraphQueueIndices.find(renderGraphName);
    auto pcbit           = primaryCommandBuffers.find(renderGraphName);
    renderContext.setRenderGraphExecutable(executable);
    for (uint32_t i = 0; i<qit->second.size(); ++i)
    {
      if (qit->second[i] == queueIndex)
      {
        pcbit->second[i]->setActiveIndex(swapChainImageIndex);
        pcbit->second[i]->cmdBegin();
        BuildCommandBufferVisitor cbVisitor(renderContext, pcbit->second[i].get(), true);
        for (auto& command : executable->commands[i])
          command->buildCommandBuffer(cbVisitor);
        pcbit->second[i]->cmdEnd();
      }
    }
  }
}

void Surface::validateSecondaryNodes()
{
  // find all secondary buffer nodes and place its data in a Surface owned vector ( is it thread friendly ?)
  FindSecondaryCommandBuffersVisitor fscbVisitor(RenderContext(this, presentationQueueIndex));
  auto v = viewer.lock();
  for (auto& rgData : renderGraphData)
  {
    auto renderGraphName = std::get<0>(rgData);
    auto executable      = v->getRenderGraphExecutable(renderGraphName);
    if(executable == nullptr)
      continue;
    fscbVisitor.renderContext.setRenderGraphExecutable(executable);
    for ( auto& commandSeq : executable->commands)
      for (auto& command : commandSeq)
        command->applyRenderContextVisitor(fscbVisitor);
  }

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
          RenderContext renderContext(this, presentationQueueIndex);
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
          RenderContext renderContext(this, presentationQueueIndex);
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
          RenderContext renderContext(this, presentationQueueIndex);
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
  std::vector<std::vector<CommandBuffer*>> commandBuffersToSubmit;
  std::vector<std::vector<VkSemaphore>>    semaphoresToSubmit;
  std::vector<VkSemaphore>                 submissionCompletedSemaphores;
  auto v = viewer.lock();
  // for each queue - collect all primary command buffers and "submission completed" semaphores in appropriate order
  for (uint32_t queueIndex = 0; queueIndex < queues.size(); ++queueIndex)
  {
    commandBuffersToSubmit.push_back(std::vector<CommandBuffer*>());
    semaphoresToSubmit.push_back(std::vector<VkSemaphore>());
    for (uint32_t rgIndex = 0; rgIndex<renderGraphData.size(); ++rgIndex)
    {
      auto renderGraphName = std::get<0>(renderGraphData[rgIndex]);
      auto executable = v->getRenderGraphExecutable(renderGraphName);
      if(executable == nullptr)
        continue;
      auto qit = renderGraphQueueIndices.find(renderGraphName);
      auto pcbit = primaryCommandBuffers.find(renderGraphName);
      auto qcbit = queueSubmissionCompletedSemaphores.find(renderGraphName);
      for (uint32_t i = 0; i < qit->second.size(); ++i)
      {
        if (qit->second[i] == queueIndex)
        {
          commandBuffersToSubmit.back().push_back(pcbit->second[i].get());
          semaphoresToSubmit.back().push_back(qcbit->second[i]);
          submissionCompletedSemaphores.push_back(qcbit->second[i]);
        }
      }
    }
  }
  // send command buffers to queues:
  // - all buffers must wait for imageAvailableSemaphore
  // - each command buffer submission ends with signaling appropriate semaphore
  for (uint32_t queueIndex = 0; queueIndex < queues.size(); ++queueIndex)
  {
    for (unsigned int i = 0; i < commandBuffersToSubmit[queueIndex].size(); ++i)
      commandBuffersToSubmit[queueIndex][i]->queueSubmit(queues[queueIndex]->queue, { imageAvailableSemaphore }, { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }, { semaphoresToSubmit[queueIndex][i] }, VK_NULL_HANDLE);
  }
  // send to rendering a command buffer that transforms swapchain image layout into VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  std::vector<VkPipelineStageFlags> waitStages;
  waitStages.resize(submissionCompletedSemaphores.size(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  presentCommandBuffer->queueSubmit(queues[presentationQueueIndex]->queue, submissionCompletedSemaphores, waitStages, { renderFinishedSemaphore }, waitFences[swapChainImageIndex]);
}

void Surface::endFrame()
{
  // present output image after its layout is transformed into VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapChain;
    presentInfo.pImageIndices      = &swapChainImageIndex;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinishedSemaphore;
    VkResult result = vkQueuePresentKHR(queues[presentationQueueIndex]->queue, &presentInfo);

  if ((result != VK_ERROR_OUT_OF_DATE_KHR) && (result != VK_SUBOPTIMAL_KHR))
    VK_CHECK_LOG_THROW(result, "failed vkQueuePresentKHR");
  window->endFrame();
}

void Surface::resizeSurface(uint32_t newWidth, uint32_t newHeight)
{
  if (!isRealized())
    return;
  if (swapChainSize.width != newWidth && swapChainSize.height != newHeight)
  {
    recreateSwapChain();
    resized = true;
  }
}

void Surface::addRenderGraph(const std::string& name, bool active)
{
  CHECK_LOG_THROW(isRealized(), "Cannot add new render graphs after surface realization");
  renderGraphData.push_back(std::make_tuple(name, active));
}

std::vector<uint32_t> Surface::getQueueIndices(const std::string renderGraphName) const
{
  auto it = renderGraphQueueIndices.find(renderGraphName);
  CHECK_LOG_THROW(it == end(renderGraphQueueIndices), "There is no render graph with name : " << renderGraphName);
  return it->second;
}

uint32_t Surface::getNumQueues() const
{
  return queues.size();
}

Queue* Surface::getQueue(uint32_t index) const
{
  return queues.at(index).get();
}

std::shared_ptr<CommandPool> Surface::getCommandPool(uint32_t index) const
{
  return commandPools.at(index);
}

void Surface::setID(std::shared_ptr<Viewer> v, uint32_t newID) 
{ 
  viewer = v;
  id = newID; 
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
  return commandPools[presentationQueueIndex];
}

std::shared_ptr<Queue> Surface::getPresentationQueue()
{
  return queues[presentationQueueIndex];
}
