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

#include <pumex/Device.h>
#include <iterator>
#include <pumex/Viewer.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Command.h>
#include <pumex/Descriptor.h>
#include <pumex/utils/Log.h>
#include <pumex/utils/Buffer.h>

using namespace pumex;

QueueTraits::QueueTraits(VkQueueFlags h, VkQueueFlags nh, float p)
  : mustHave{ h }, mustNotHave{ nh }, priority(p)
{
}

Queue::Queue(const QueueTraits& t, uint32_t f, uint32_t i, VkQueue vq)
  : traits(t), familyIndex(f), index(i), queue(vq), available(true)
{
}

Device::Device(std::shared_ptr<Viewer> v, std::shared_ptr<PhysicalDevice> d, const std::vector<std::string>& re )
  : viewer{ v }, physical{ d }, device{ VK_NULL_HANDLE }
{
  for (const auto& extension : re)
    requestedDeviceExtensions.push_back(extension.c_str());
}

Device::~Device()
{
  cleanup();
}

void Device::realize()
{
  if (isRealized())
    return;

  auto physicalDevice = physical.lock();

  // we have to assign queues to available queue families
  std::vector<std::vector<uint32_t>> matchingFamilies;
  for (const auto& queueTraits : requestedQueues)
    matchingFamilies.push_back(physicalDevice->matchingFamilyIndices(queueTraits));

  std::vector<uint32_t> queueCount(physicalDevice->queueFamilyProperties.size());
  for (uint32_t i = 0; i < queueCount.size(); i++)
    queueCount[i] = physicalDevice->queueFamilyProperties[i].queueCount;

  std::vector<uint32_t> chosenFamilies(requestedQueues.size());
  std::fill(begin(chosenFamilies), end(chosenFamilies), std::numeric_limits<uint32_t>::max());
  
  // at first - assign queues that have only one queue family available
  for (uint32_t i = 0; i < requestedQueues.size(); i++)
  {
    if (matchingFamilies[i].size() > 1)
      continue;
    CHECK_LOG_THROW(queueCount[matchingFamilies[i][0]]==0, "Device cannot deliver requested queues (1)");
    chosenFamilies[i] = matchingFamilies[i][0];
    queueCount[matchingFamilies[i][0]] -= 1;
  }
  // assign other queues in no particular order ( first fit :) )
  for (uint32_t i = 0; i < requestedQueues.size(); i++)
  {
    if (chosenFamilies[i] != std::numeric_limits<uint32_t>::max())
      continue;
    bool found = false;
    for (uint32_t j = 0; j < matchingFamilies[i].size(); ++j)
    {
      if (queueCount[matchingFamilies[i][j]] == 0)
        continue;
      chosenFamilies[i] = matchingFamilies[i][j];
      queueCount[matchingFamilies[i][j]] -= 1;
      found = true;
    }
    CHECK_LOG_THROW(!found, "Device cannot deliver requested queues (2)");
  }

  std::map<uint32_t, std::vector<float>> groupedRequests;
  for (uint32_t i = 0; i < chosenFamilies.size(); ++i)
  {
    auto it = groupedRequests.find(chosenFamilies[i]);
    if (it == end(groupedRequests))
      it = groupedRequests.insert({ chosenFamilies[i], std::vector<float>() }).first;
    it->second.push_back(requestedQueues[i].priority);
  }

  std::vector<VkDeviceQueueCreateInfo> deviceQueues;
  for (const auto& req : groupedRequests)
  {
    VkDeviceQueueCreateInfo queueCreateInfo{};
      queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueCreateInfo.queueFamilyIndex = req.first;
      queueCreateInfo.queueCount       = req.second.size();
      queueCreateInfo.pQueuePriorities = req.second.data();
    deviceQueues.push_back( queueCreateInfo );
  }

  VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = deviceQueues.size();
    deviceCreateInfo.pQueueCreateInfos    = deviceQueues.data();
    deviceCreateInfo.pEnabledFeatures     = &physicalDevice->features;

  if (viewer.lock()->viewerTraits.useDebugLayers() && physicalDevice->deviceExtensionImplemented(VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
  {
    enabledDeviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
    enableDebugMarkers = true;
  }

  std::copy( cbegin(requestedDeviceExtensions), cend(requestedDeviceExtensions), std::back_inserter(enabledDeviceExtensions) );

  if (enabledDeviceExtensions.size() > 0)
  {
    deviceCreateInfo.enabledExtensionCount = (uint32_t)enabledDeviceExtensions.size();
    deviceCreateInfo.ppEnabledExtensionNames = enabledDeviceExtensions.data();
  }

  VK_CHECK_LOG_THROW( vkCreateDevice(physicalDevice->physicalDevice, &deviceCreateInfo, nullptr, &device), "Could not create logical device" );

  // collect all created queues
  std::vector<uint32_t> queueIndex(physicalDevice->queueFamilyProperties.size());
  std::fill(begin(queueIndex), end(queueIndex), 0);
  for (uint32_t i=0; i<requestedQueues.size(); ++i)
  {
    VkQueue queue;
    vkGetDeviceQueue(device, chosenFamilies[i], queueIndex[chosenFamilies[i]], &queue);
    CHECK_LOG_THROW(queue == VK_NULL_HANDLE, "Could not get the queue " << chosenFamilies[i] << " " << queueIndex[chosenFamilies[i]]);
    queues.push_back( std::make_shared<Queue>(requestedQueues[i], chosenFamilies[i], queueIndex[chosenFamilies[i]], queue ));
    queueIndex[chosenFamilies[i]] += 1;
  }
  if (enableDebugMarkers)
  {
    pfnDebugMarkerSetObjectTag  = reinterpret_cast<PFN_vkDebugMarkerSetObjectTagEXT>(vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectTagEXT"));
    pfnDebugMarkerSetObjectName = reinterpret_cast<PFN_vkDebugMarkerSetObjectNameEXT>(vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectNameEXT"));
    pfnCmdDebugMarkerBegin      = reinterpret_cast<PFN_vkCmdDebugMarkerBeginEXT>(vkGetDeviceProcAddr(device, "vkCmdDebugMarkerBeginEXT"));
    pfnCmdDebugMarkerEnd        = reinterpret_cast<PFN_vkCmdDebugMarkerEndEXT>(vkGetDeviceProcAddr(device, "vkCmdDebugMarkerEndEXT"));
    pfnCmdDebugMarkerInsert     = reinterpret_cast<PFN_vkCmdDebugMarkerInsertEXT>(vkGetDeviceProcAddr(device, "vkCmdDebugMarkerInsertEXT"));
  }

  // create descriptor pool
  descriptorPool = std::make_shared<DescriptorPool>();
}

void Device::cleanup()
{
  if (device != VK_NULL_HANDLE)
  {
    stagingBuffers.clear();
    descriptorPool = nullptr;
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
    queues.clear();
  }
}

std::shared_ptr<Queue> Device::getQueue(const QueueTraits& queueTraits, bool reserve)
{
  for (auto& q : queues)
  {
    if (q->traits!=queueTraits)
      continue;
    if ( !q->available )
      continue;
    if ( reserve )
      q->available = false;
    return q;
  }
  return std::shared_ptr<Queue>();
}

void Device::releaseQueue(std::shared_ptr<Queue> queue)
{
  for (auto& q : queues)
  {
    if (q->queue != queue->queue)
      continue;
    q->available = true;
  }
}

std::shared_ptr<DescriptorPool> Device::getDescriptorPool()
{
  return descriptorPool;
}

std::shared_ptr<StagingBuffer> Device::acquireStagingBuffer(const void* data, VkDeviceSize size)
{
  // find smallest staging buffer that is able to transfer data
  VkDeviceSize smallestSize = std::numeric_limits<VkDeviceSize>::max();
  std::shared_ptr<StagingBuffer> resultBuffer;

  std::lock_guard<std::mutex> lock(stagingMutex);
  for (auto it = begin(stagingBuffers); it != end(stagingBuffers); ++it)
  {
    if ( (*it)->isReserved() || (*it)->bufferSize() < size)
      continue;
    if ((*it)->bufferSize() < smallestSize)
    {
      smallestSize = (*it)->bufferSize();
      resultBuffer = *it;
    }
  }
  if (resultBuffer.get() == nullptr)
  {
    resultBuffer = std::make_shared<StagingBuffer>( this, size );
    stagingBuffers.push_back(resultBuffer);
  }
  resultBuffer->setReserved(true);
  if (data != nullptr)
  {
    resultBuffer->fillBuffer(data, size);
  }
  return resultBuffer;

}

void Device::releaseStagingBuffer(std::shared_ptr<StagingBuffer> buffer)
{
  std::lock_guard<std::mutex> lock(stagingMutex);
  buffer->setReserved(false);
}

bool Device::deviceExtensionEnabled(const char* extensionName) const
{
  for (const auto& e : enabledDeviceExtensions)
    if (!std::strcmp(extensionName, e))
      return true;
  return false;
}

std::shared_ptr<CommandBuffer> Device::beginSingleTimeCommands(std::shared_ptr<CommandPool> commandPool)
{
  std::lock_guard<std::mutex> lock(submitMutex);
  std::shared_ptr<CommandBuffer> commandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, this, commandPool);
  commandBuffer->cmdBegin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
  return commandBuffer;
}

void Device::endSingleTimeCommands(std::shared_ptr<CommandBuffer> commandBuffer, VkQueue queue, bool submit)
{
  std::lock_guard<std::mutex> lock(submitMutex);
  commandBuffer->cmdEnd();
  if (submit)
  {
    VkFence fence;
    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = 0;
    VK_CHECK_LOG_THROW(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence), "Cannot create fence");

    commandBuffer->queueSubmit(queue, {}, {}, {}, fence);
    VK_CHECK_LOG_THROW(vkWaitForFences(device, 1, &fence, VK_TRUE, 100000000000), "Waiting for a fence failed");

    vkDestroyFence(device, fence, nullptr);
  }
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
    VkDebugMarkerObjectTagInfoEXT tagInfo{};
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
    VkDebugMarkerMarkerInfoEXT markerInfo{};
    markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
    std::memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
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
      std::memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
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

