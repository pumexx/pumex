#pragma once
#include <vector>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

class Device;
class RenderPass;
class ComputePipeline;
class GraphicsPipeline;
class PipelineLayout;
class DescriptorSet;
class Image;

class PUMEX_EXPORT CommandPool
{
public:
  CommandPool()                              = delete;
  explicit CommandPool(uint32_t queueFamilyIndex);
  CommandPool(const CommandPool&)            = delete;
  CommandPool& operator=(const CommandPool&) = delete;
  virtual ~CommandPool();

  void          validate(std::shared_ptr<pumex::Device> device);
  VkCommandPool getHandle(VkDevice device) const;

  uint32_t queueFamilyIndex;
protected:
  struct PerDeviceData
  {
    VkCommandPool commandPool = VK_NULL_HANDLE;
  };
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

struct PipelineBarrier;

class PUMEX_EXPORT CommandBuffer
{
public:
  explicit CommandBuffer(VkCommandBufferLevel bufferLevel, std::shared_ptr<Device> device, std::shared_ptr<CommandPool> commandPool, uint32_t cbCount = 1);
  CommandBuffer(const CommandBuffer&)            = delete;
  CommandBuffer& operator=(const CommandBuffer&) = delete;
  virtual ~CommandBuffer();

  inline void setActiveIndex(uint32_t index);
  inline uint32_t getActiveIndex() const;

  VkCommandBuffer getHandle() const;

  void cmdBegin(VkCommandBufferUsageFlags usageFlags = 0) const;
  void cmdEnd() const;

  void cmdBeginRenderPass(std::shared_ptr<pumex::RenderPass> renderPass, VkFramebuffer frameBuffer, VkRect2D renderArea, const std::vector<VkClearValue>& clearValues) const;
  void cmdNextSubPass(VkSubpassContents contents) const;
  void cmdEndRenderPass() const;

  void cmdSetViewport(uint32_t firstViewport, const std::vector<VkViewport> viewports) const;
  void cmdSetScissor(uint32_t firstScissor, const std::vector<VkRect2D> scissors) const;

  void cmdPipelineBarrier(VkPipelineStageFlagBits srcStageMask, VkPipelineStageFlagBits dstStageMask, VkDependencyFlags dependencyFlags, const std::vector<PipelineBarrier>& barriers) const;
  void cmdPipelineBarrier(VkPipelineStageFlagBits srcStageMask, VkPipelineStageFlagBits dstStageMask, VkDependencyFlags dependencyFlags, const PipelineBarrier& barrier) const;
  void cmdCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, std::vector<VkBufferCopy> bufferCopy) const;
  void cmdCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, const VkBufferCopy& bufferCopy) const;
  void cmdBindPipeline(std::shared_ptr<pumex::ComputePipeline> pipeline) const;
  void cmdBindPipeline(std::shared_ptr<pumex::GraphicsPipeline> pipeline) const;
  void cmdBindDescriptorSets(VkPipelineBindPoint bindPoint, std::shared_ptr<pumex::PipelineLayout> pipelineLayout, uint32_t firstSet, const std::vector<std::shared_ptr<pumex::DescriptorSet>> descriptorSets) const;
  void cmdBindDescriptorSets(VkPipelineBindPoint bindPoint, std::shared_ptr<pumex::PipelineLayout> pipelineLayout, uint32_t firstSet, std::shared_ptr<pumex::DescriptorSet> descriptorSet) const;

  void cmdDrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) const;
  void cmdDrawIndexedIndirect(VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) const;
  void cmdDispatch(uint32_t x, uint32_t y, uint32_t z) const;

  void cmdCopyBufferToImage(VkBuffer srcBuffer, const Image& image, VkImageLayout dstImageLayout, const std::vector<VkBufferImageCopy>& regions) const;

  void setImageLayout(Image& image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkImageSubresourceRange subresourceRange) const;
  void setImageLayout(Image& image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout) const;



  // submit queue - no fences and semaphores
  void queueSubmit(VkQueue queue, const std::vector<VkSemaphore>& waitSemaphores = {}, const std::vector<VkPipelineStageFlags>& waitStages = {}, const std::vector<VkSemaphore>& signalSemaphores = {}, VkFence fence = VK_NULL_HANDLE) const;

  VkCommandBufferLevel         bufferLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  std::shared_ptr<CommandPool> commandPool;
protected:
  VkDevice                     device        = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> commandBuffer;
  uint32_t                     activeIndex   = 0;
};

void     CommandBuffer::setActiveIndex(uint32_t index) { activeIndex = index % commandBuffer.size(); }
uint32_t CommandBuffer::getActiveIndex() const         { return activeIndex; }

struct PUMEX_EXPORT PipelineBarrier
{
  PipelineBarrier() = delete;
  // ordinary memory barrier
  explicit PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask);
  // buffer barrier
  explicit PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);
  explicit PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkDescriptorBufferInfo bufferInfo);
  // image barrier
  explicit PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkImage image, VkImageSubresourceRange subresourceRange);

  enum Type { Undefined, Memory, Image, Buffer };
  Type mType = Undefined;
  union
  {
    VkMemoryBarrier       memoryBarrier;
    VkBufferMemoryBarrier bufferBarrier;
    VkImageMemoryBarrier  imageBarrier;
  };
};

inline VkRect2D makeVkRect2D(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
  VkRect2D rect{};
    rect.offset.x      = x;
    rect.offset.y      = y;
    rect.extent.width  = width;
    rect.extent.height = height;
  return rect;
}

inline VkViewport makeViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
{
  VkViewport viewport{};
    viewport.x        = x;
    viewport.y        = y;
    viewport.width    = width;
    viewport.height   = height;
    viewport.minDepth = minDepth;
    viewport.maxDepth = maxDepth;
  return viewport;
}

inline VkClearValue makeColorClearValue(const glm::vec4 color)
{
  VkClearValue value;
    value.color.float32[0] = color.r;
    value.color.float32[1] = color.g;
    value.color.float32[2] = color.b;
    value.color.float32[3] = color.a;
  return value;
}

inline VkClearValue makeDepthStencilClearValue(float depth, uint32_t stencil)
{
  VkClearValue value;
    value.depthStencil.depth = depth;
    value.depthStencil.stencil = stencil;
  return value;
}



}