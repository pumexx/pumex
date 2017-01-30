#include <pumex/Device.h>
#include <iterator>
#include <pumex/Viewer.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Command.h>
#include <pumex/utils/Log.h>

using namespace pumex;

QueueTraits::QueueTraits(VkQueueFlags h, VkQueueFlags nh, const std::vector<float>& p)
  : mustHave{ h }, mustNotHave{ nh }, priority(p)
{
}

Device::Device(std::shared_ptr<pumex::Viewer> const& v, std::shared_ptr<pumex::PhysicalDevice> const& d, const std::vector<pumex::QueueTraits>& requestedQueues, const std::vector<const char*>& requestedExtensions)
  : viewer{ v }, physical{ d }, device{ VK_NULL_HANDLE }
{
  auto physicalDevice = physical.lock();

  // we have to assign queues to available queue families
  std::vector<std::vector<uint32_t>> matchingFamilies;
  for (const auto& queueTraits : requestedQueues)
    matchingFamilies.push_back(physicalDevice->matchingFamilyIndices(queueTraits));

  std::vector<std::vector<pumex::QueueTraits>> proposedMatching( physicalDevice->queueFamilyProperties.size() );
  std::vector<std::vector<float>>              proposedMatchingPriorities(physicalDevice->queueFamilyProperties.size());

  // we will start from queues that have lowest number of matching family indices
  for (uint32_t s = 1; s <= physicalDevice->queueFamilyProperties.size(); ++s)
  {
    for (uint32_t i = 0; i < requestedQueues.size(); ++i)
    {
      if ( s != matchingFamilies[i].size() )
        continue;
      // let's check if specific family has a place for these queues
      bool queuesPlaced = false;
      for (uint32_t j = 0; j < matchingFamilies[i].size(); ++j)
      {
        uint32_t q = matchingFamilies[i][j];
        // i : requestedQueues
        // q : proposedMatching, proposedMatchingCount, queueFamilyProperties
        // if number of requested queues plus number of already matched queues is higher than possible number of queues in that family
        if (requestedQueues[i].priority.size() + proposedMatchingPriorities[q].size() > physicalDevice->queueFamilyProperties[q].queueCount)
          continue;
        proposedMatching[q].push_back(requestedQueues[i]);
        std::copy(requestedQueues[i].priority.begin(), requestedQueues[i].priority.end(), std::back_inserter(proposedMatchingPriorities[q]));
        queuesPlaced = true;
      }
      CHECK_LOG_THROW(!queuesPlaced, "Device cannot deliver requested queues" );
    }
  }

  std::vector<VkDeviceQueueCreateInfo> deviceQueues;
  for (uint32_t q=0; q<proposedMatching.size(); ++q)
  {
    if (proposedMatchingPriorities[q].size() == 0)
      continue;

    VkDeviceQueueCreateInfo queueCreateInfo{};
      queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueCreateInfo.queueFamilyIndex = q;
      queueCreateInfo.queueCount       = proposedMatchingPriorities[q].size();
      queueCreateInfo.pQueuePriorities = proposedMatchingPriorities[q].data();

    deviceQueues.push_back( queueCreateInfo );
  }

  VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(deviceQueues.size());
    deviceCreateInfo.pQueueCreateInfos    = deviceQueues.data();

  std::vector<const char*> deviceExtensions;
  // Enable the debug marker extension if it is present (likely meaning a debugging tool is present)
  if (physicalDevice->hasExtension(VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
  {
    deviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
    enableDebugMarkers = true;
  }

  std::copy( requestedExtensions.cbegin(), requestedExtensions.cend(), std::back_inserter(deviceExtensions) );

  if (deviceExtensions.size() > 0)
  {
    deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
  }

  VK_CHECK_LOG_THROW( vkCreateDevice(physicalDevice->physicalDevice, &deviceCreateInfo, nullptr, &device), "Could not create logical device" );
  // get all created queues
  for (uint32_t q = 0; q<proposedMatching.size(); ++q)
  {
    uint32_t k = 0;
    for (uint32_t i = 0; i < proposedMatching[q].size(); ++i)
    {
      for (uint32_t j = 0; j < proposedMatching[q][i].priority.size(); ++j, ++k)
      {
        VkQueue queue;
        vkGetDeviceQueue(device, q, k, &queue);
        CHECK_LOG_THROW(queue == VK_NULL_HANDLE, "Could not get the queue " << q << " " << k);
        QueueTraits queueTraits(proposedMatching[q][i].mustHave, proposedMatching[q][i].mustNotHave, { proposedMatching[q][i].priority[j] });
        queues.push_back(Queue{ queueTraits, q, k, queue });
      }
    }
  }
  if (enableDebugMarkers)
  {
    pfnDebugMarkerSetObjectTag  = reinterpret_cast<PFN_vkDebugMarkerSetObjectTagEXT>(vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectTagEXT"));
    pfnDebugMarkerSetObjectName = reinterpret_cast<PFN_vkDebugMarkerSetObjectNameEXT>(vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectNameEXT"));
    pfnCmdDebugMarkerBegin      = reinterpret_cast<PFN_vkCmdDebugMarkerBeginEXT>(vkGetDeviceProcAddr(device, "vkCmdDebugMarkerBeginEXT"));
    pfnCmdDebugMarkerEnd        = reinterpret_cast<PFN_vkCmdDebugMarkerEndEXT>(vkGetDeviceProcAddr(device, "vkCmdDebugMarkerEndEXT"));
    pfnCmdDebugMarkerInsert     = reinterpret_cast<PFN_vkCmdDebugMarkerInsertEXT>(vkGetDeviceProcAddr(device, "vkCmdDebugMarkerInsertEXT"));
  }
}

Device::~Device()
{
  cleanup();
}

void Device::cleanup()
{
  if (device != VK_NULL_HANDLE)
  {
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
    queues.clear();
  }
}

VkQueue Device::getQueue(const pumex::QueueTraits& queueTraits, bool reserve)
{
  for (auto& q : queues)
  {
    if (!q.isEqual(queueTraits))
      continue;
    if ( !q.available )
      continue;
    if ( reserve )
      q.available = false;
    return q.queue;
  }
  return VK_NULL_HANDLE;
}

bool Device::getQueueIndices(VkQueue queue, std::tuple<uint32_t&, uint32_t&>& result)
{
  for (auto& q : queues)
  {
    if (q.queue == queue)
    {
      result = std::make_tuple(q.familyIndex,q.index);
      return true;
    }
  }
  return false;
}

void Device::releaseQueue(VkQueue queue)
{
  for (auto& q : queues)
  {
    if (q.queue != queue)
      continue;
    q.available = true;
  }
}



std::shared_ptr<pumex::CommandBuffer> Device::beginSingleTimeCommands(std::shared_ptr<pumex::CommandPool> commandPool)
//VkCommandBuffer Device::beginSingleTimeCommands(VkCommandPool commandPool)
{
  std::shared_ptr<pumex::CommandBuffer> commandBuffer = std::make_shared<pumex::CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, commandPool);
  commandBuffer->validate(shared_from_this());
  commandBuffer->cmdBegin(shared_from_this(), VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
  //VkCommandBufferAllocateInfo allocInfo{};
  //  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  //  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  //  allocInfo.commandPool = commandPool;
  //  allocInfo.commandBufferCount = 1;

  //VkCommandBuffer commandBuffer;
  //vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  //VkCommandBufferBeginInfo beginInfo = {};
  //  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  //  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  //vkBeginCommandBuffer(commandBuffer, &beginInfo);
  return commandBuffer;
}

void Device::endSingleTimeCommands(std::shared_ptr<pumex::CommandBuffer> commandBuffer, VkQueue queue)
//void Device::endSingleTimeCommands(VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue)
{
  commandBuffer->cmdEnd(shared_from_this());

  VkFence fence;
  VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = 0;
  VK_CHECK_LOG_THROW( vkCreateFence(device, &fenceCreateInfo, nullptr, &fence), "Cannot create fence");

  commandBuffer->queueSubmit(shared_from_this(), queue, {}, {}, {}, fence);
  VK_CHECK_LOG_THROW(vkWaitForFences(device, 1, &fence, VK_TRUE, 100000000000), "Waiting for a fence failed");

  vkDestroyFence(device, fence, nullptr);
  commandBuffer.reset();
}

void Device::setObjectName(uint64_t object, VkDebugReportObjectTypeEXT objectType, const std::string& name)
{
  // Check for valid function pointer (may not be present if not running in a debugging application)
  if (pfnDebugMarkerSetObjectName != VK_NULL_HANDLE)
  {
    VkDebugMarkerObjectNameInfoEXT nameInfo{};
      nameInfo.sType       = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
      nameInfo.objectType  = objectType;
      nameInfo.object      = object;
      nameInfo.pObjectName = name.c_str();
    pfnDebugMarkerSetObjectName(device, &nameInfo);
  }
}

void Device::setObjectTag(uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void* tag)
{
  // Check for valid function pointer (may not be present if not running in a debugging application)
  if (pfnDebugMarkerSetObjectTag != VK_NULL_HANDLE)
  {
    VkDebugMarkerObjectTagInfoEXT tagInfo = {};
      tagInfo.sType      = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_TAG_INFO_EXT;
      tagInfo.objectType = objectType;
      tagInfo.object     = object;
      tagInfo.tagName    = name;
      tagInfo.tagSize    = tagSize;
      tagInfo.pTag       = tag;
    pfnDebugMarkerSetObjectTag(device, &tagInfo);
  }
}

void Device::beginMarkerRegion(VkCommandBuffer cmdbuffer, const std::string& markerName, glm::vec4 color)
{
  // Check for valid function pointer (may not be present if not running in a debugging application)
  if (pfnCmdDebugMarkerBegin != VK_NULL_HANDLE)
  {
    VkDebugMarkerMarkerInfoEXT markerInfo = {};
    markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
    memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
    markerInfo.pMarkerName = markerName.c_str();
    pfnCmdDebugMarkerBegin(cmdbuffer, &markerInfo);
  }
}

void Device::insertMarker(VkCommandBuffer cmdbuffer, const std::string& markerName, glm::vec4 color)
{
  // Check for valid function pointer (may not be present if not running in a debugging application)
  if (pfnCmdDebugMarkerInsert != VK_NULL_HANDLE)
  {
    VkDebugMarkerMarkerInfoEXT markerInfo{};
      markerInfo.sType       = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
      markerInfo.pMarkerName = markerName.c_str();
      memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
    pfnCmdDebugMarkerInsert(cmdbuffer, &markerInfo);
  }
}

void Device::endMarkerRegion(VkCommandBuffer cmdBuffer)
{
  // Check for valid function (may not be present if not runnin in a debugging application)
  if (pfnCmdDebugMarkerEnd != VK_NULL_HANDLE)
  {
    pfnCmdDebugMarkerEnd(cmdBuffer);
  }
}

void Device::setCommandBufferName(VkCommandBuffer cmdBuffer, const std::string& name)
{
  setObjectName((uint64_t)cmdBuffer, VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT, name);
}

void Device::setQueueName(VkQueue queue, const std::string& name)
{
  setObjectName((uint64_t)queue, VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT, name);
}

void Device::setImageName(VkImage image, const std::string& name)
{
  setObjectName((uint64_t)image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, name);
}

void Device::setSamplerName(VkSampler sampler, const std::string& name)
{
  setObjectName((uint64_t)sampler, VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, name);
}

void Device::setBufferName(VkBuffer buffer, const std::string& name)
{
  setObjectName((uint64_t)buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, name);
}

void Device::setDeviceMemoryName(VkDeviceMemory memory, const std::string& name)
{
  setObjectName((uint64_t)memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, name);
}

void Device::setShaderModuleName(VkShaderModule shaderModule, const std::string& name)
{
  setObjectName((uint64_t)shaderModule, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, name);
}

void Device::setPipelineName(VkPipeline pipeline, const std::string& name)
{
  setObjectName((uint64_t)pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, name);
}

void Device::setPipelineLayoutName(VkPipelineLayout pipelineLayout, const std::string& name)
{
  setObjectName((uint64_t)pipelineLayout, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT, name);
}

void Device::setRenderPassName(VkRenderPass renderPass, const std::string& name)
{
  setObjectName((uint64_t)renderPass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, name);
}

void Device::setFramebufferName(VkFramebuffer framebuffer, const std::string& name)
{
  setObjectName((uint64_t)framebuffer, VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, name);
}

void Device::setDescriptorSetLayoutName(VkDescriptorSetLayout descriptorSetLayout, const std::string& name)
{
  setObjectName((uint64_t)descriptorSetLayout, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, name);
}

void Device::setDescriptorSetName(VkDescriptorSet descriptorSet, const std::string& name)
{
  setObjectName((uint64_t)descriptorSet, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, name);
}

void Device::setSemaphoreName(VkSemaphore semaphore, const std::string& name)
{
  setObjectName((uint64_t)semaphore, VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT, name);
}

void Device::setFenceName(VkFence fence, const std::string& name)
{
  setObjectName((uint64_t)fence, VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT, name);
}

void Device::setEventName(VkEvent _event, const std::string& name)
{
  setObjectName((uint64_t)_event, VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT, name);
}

Device::Queue::Queue(const pumex::QueueTraits& t, uint32_t f, uint32_t i, VkQueue vq)
  : traits(t), familyIndex(f), index(i), queue(vq), available(true)
{
}
bool Device::Queue::isEqual(const pumex::QueueTraits& queueTraits)
{
  return (traits.mustHave == queueTraits.mustHave) && (traits.mustNotHave == queueTraits.mustNotHave) && (traits.priority[0] == queueTraits.priority[0]);
}
