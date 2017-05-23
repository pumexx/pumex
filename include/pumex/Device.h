#pragma once
#include <vector>
#include <memory>
#include <tuple>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <glm/glm.hpp>

namespace pumex
{

class Viewer;
class PhysicalDevice;
class CommandPool;
class CommandBuffer;

// struct that represents queues that must be provided by Vulkan device
struct PUMEX_EXPORT QueueTraits
{
  QueueTraits(VkQueueFlags mustHave, VkQueueFlags mustNotHave, const std::vector<float>& priority);

  VkQueueFlags       mustHave    = 0;
  VkQueueFlags       mustNotHave = 0;
  std::vector<float> priority;
};

// class representing Vulkan logical device
class PUMEX_EXPORT Device : public std::enable_shared_from_this<Device>
{
public:
  Device()                         = delete;
  explicit Device(std::shared_ptr<pumex::Viewer> const& viewer, std::shared_ptr<pumex::PhysicalDevice> const& physical, const std::vector<pumex::QueueTraits>& requestedQueues, const std::vector<const char*>& requestedExtensions);
  Device(const Device&)            = delete;
  Device& operator=(const Device&) = delete;
  ~Device();

  inline bool isValid();
  void cleanup();

  std::shared_ptr<pumex::CommandBuffer> beginSingleTimeCommands(std::shared_ptr<pumex::CommandPool> commandPool);
  void endSingleTimeCommands(std::shared_ptr<pumex::CommandBuffer> commandBuffer, VkQueue queue);

  VkQueue getQueue(const pumex::QueueTraits& queueTraits, bool reserve = false);
  void releaseQueue(VkQueue queue);
  bool getQueueIndices(VkQueue queue, std::tuple<uint32_t&, uint32_t&>& result);

  // debug markers extension stuff - not tested yet
  void setObjectName(uint64_t object, VkDebugReportObjectTypeEXT objectType, const std::string& name);
  void setObjectTag(uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void* tag);
  void beginMarkerRegion(VkCommandBuffer cmdbuffer, const std::string& markerName, glm::vec4 color);
  void insertMarker(VkCommandBuffer cmdbuffer, const std::string& markerName, glm::vec4 color);
  void endMarkerRegion(VkCommandBuffer cmdBuffer);
  void setCommandBufferName(VkCommandBuffer cmdBuffer, const std::string& name);
  void setQueueName(VkQueue queue, const std::string& name);
  void setImageName(VkImage image, const std::string& name);
  void setSamplerName(VkSampler sampler, const std::string& name);
  void setBufferName(VkBuffer buffer, const std::string& name);
  void setDeviceMemoryName(VkDeviceMemory memory, const std::string& name);
  void setShaderModuleName(VkShaderModule shaderModule, const std::string& name);
  void setPipelineName(VkPipeline pipeline, const std::string& name);
  void setPipelineLayoutName(VkPipelineLayout pipelineLayout, const std::string& name);
  void setRenderPassName(VkRenderPass renderPass, const std::string& name);
  void setFramebufferName(VkFramebuffer framebuffer, const std::string& name);
  void setDescriptorSetLayoutName(VkDescriptorSetLayout descriptorSetLayout, const std::string& name);
  void setDescriptorSetName(VkDescriptorSet descriptorSet, const std::string& name);
  void setSemaphoreName(VkSemaphore semaphore, const std::string& name);
  void setFenceName(VkFence fence, const std::string& name);
  void setEventName(VkEvent _event, const std::string& name);

	std::weak_ptr<pumex::Viewer>         viewer;
	std::weak_ptr<pumex::PhysicalDevice> physical;
  VkDevice                             device             = VK_NULL_HANDLE;
  bool                                 enableDebugMarkers = false;
protected:
  struct Queue
  {
    Queue(const pumex::QueueTraits& queueTraits, uint32_t familyIndex, uint32_t index, VkQueue queue);
    bool isEqual(const pumex::QueueTraits& queueTraits);

    pumex::QueueTraits traits;
    uint32_t           familyIndex = UINT32_MAX;
    uint32_t           index       = UINT32_MAX;
    VkQueue            queue       = VK_NULL_HANDLE;
    bool               available   = true;
  };

  std::vector<Queue> queues;

  PFN_vkDebugMarkerSetObjectTagEXT  pfnDebugMarkerSetObjectTag  = VK_NULL_HANDLE;
  PFN_vkDebugMarkerSetObjectNameEXT pfnDebugMarkerSetObjectName = VK_NULL_HANDLE;
  PFN_vkCmdDebugMarkerBeginEXT      pfnCmdDebugMarkerBegin      = VK_NULL_HANDLE;
  PFN_vkCmdDebugMarkerEndEXT        pfnCmdDebugMarkerEnd        = VK_NULL_HANDLE;
  PFN_vkCmdDebugMarkerInsertEXT     pfnCmdDebugMarkerInsert     = VK_NULL_HANDLE;

};

bool Device::isValid() { return device != VK_NULL_HANDLE; }

}
