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

#pragma once
#include <memory>
#include <vector>
#include <set>
#include <unordered_map>
#include <mutex>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <pumex/Export.h>

namespace pumex
{

class  Device;
class  Surface;
class  RenderContext;
class  RenderSubPass;
class  FrameBuffer;
class  ComputePipeline;
class  GraphicsPipeline;
class  PipelineLayout;
class  DescriptorSet;
class  Image;
struct MemoryObjectBarrierGroup;
class  MemoryObjectBarrier;

class PUMEX_EXPORT CommandPool
{
public:
  CommandPool()                              = delete;
  explicit CommandPool(uint32_t queueFamilyIndex);
  CommandPool(const CommandPool&)            = delete;
  CommandPool(CommandPool&&)                 = delete;
  CommandPool& operator=(const CommandPool&) = delete;
  CommandPool& operator=(CommandPool&&)      = delete;
  virtual ~CommandPool();

  void          validate(Device* device);
  VkCommandPool getHandle(VkDevice device) const;

  uint32_t queueFamilyIndex;
protected:
  struct PerDeviceData
  {
    VkCommandPool commandPool = VK_NULL_HANDLE;
  };
  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

struct PipelineBarrier;

class CommandBufferSource;

// Class representing Vulkan command buffer. Most of the vkCmd* commands will be defined here. 
class PUMEX_EXPORT CommandBuffer
{
public:
  CommandBuffer()                                = delete;
  explicit CommandBuffer(VkCommandBufferLevel bufferLevel, Device* device, std::shared_ptr<CommandPool> commandPool, uint32_t cbCount = 1);
  CommandBuffer(const CommandBuffer&)            = delete;
  CommandBuffer& operator=(const CommandBuffer&) = delete;
  CommandBuffer(CommandBuffer&&)                 = delete;
  CommandBuffer& operator=(CommandBuffer&&)      = delete;
  virtual ~CommandBuffer();

  inline void     setActiveIndex(uint32_t index);
  inline uint32_t getActiveIndex() const;

  void            invalidate(uint32_t index);
  inline bool     isValid();

  void            addSource(CommandBufferSource* source);
  void            clearSources();

  VkCommandBuffer getHandle() const;

  // Vulkan commands
  void            cmdBegin(VkCommandBufferUsageFlags usageFlags = 0, VkRenderPass renderPass = VK_NULL_HANDLE, uint32_t subPass = 0);
  void            cmdEnd();

  void            cmdBeginRenderPass(const RenderContext& renderContext, RenderSubPass* renderSubPass, VkRect2D renderArea, const std::vector<VkClearValue>& clearValues, VkSubpassContents subpassContents);
  void            cmdNextSubPass(RenderSubPass* renderSubPass, VkSubpassContents contents);
  void            cmdEndRenderPass() const;

  void            cmdSetViewport(uint32_t firstViewport, const std::vector<VkViewport> viewports) const;
  void            cmdSetScissor(uint32_t firstScissor, const std::vector<VkRect2D> scissors) const;

  void            cmdPipelineBarrier(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, const std::vector<PipelineBarrier>& barriers) const;
  void            cmdPipelineBarrier(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, const PipelineBarrier& barrier) const;
  void            cmdPipelineBarrier(const RenderContext& renderContext, const MemoryObjectBarrierGroup& barrierGroup, const std::vector<MemoryObjectBarrier>& barriers);
  void            cmdCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, std::vector<VkBufferCopy> bufferCopy) const;
  void            cmdCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, const VkBufferCopy& bufferCopy) const;

  void            cmdBindPipeline(const RenderContext& renderContext, ComputePipeline* pipeline);
  void            cmdBindPipeline(const RenderContext& renderContext, GraphicsPipeline* pipeline);
  void            cmdBindDescriptorSets(const RenderContext& renderContext, PipelineLayout* pipelineLayout, uint32_t firstSet, const std::vector<DescriptorSet*> descriptorSets);
  void            cmdBindDescriptorSets(const RenderContext& renderContext, PipelineLayout* pipelineLayout, uint32_t firstSet, DescriptorSet* descriptorSet);

  void            cmdDraw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t vertexOffset, uint32_t firstInstance) const;
  void            cmdDrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) const;
  void            cmdDrawIndexedIndirect(VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) const;
  void            cmdDispatch(uint32_t x, uint32_t y, uint32_t z) const;

  void            cmdCopyBufferToImage(VkBuffer srcBuffer, const Image& image, VkImageLayout dstImageLayout, const std::vector<VkBufferImageCopy>& regions) const;
  void            cmdClearColorImage(const Image& image, VkImageLayout imageLayout, VkClearValue color, std::vector<VkImageSubresourceRange> subresourceRanges);
  void            cmdClearDepthStencilImage(const Image& image, VkImageLayout imageLayout, VkClearValue depthStencil, std::vector<VkImageSubresourceRange> subresourceRanges);

  void            setImageLayout(Image& image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkImageSubresourceRange subresourceRange) const;
  void            setImageLayout(Image& image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout) const;

  void            executeCommandBuffer(const RenderContext& renderContext, CommandBuffer* secondaryBuffer);

  // submit queue - no fences and semaphores
  void queueSubmit(VkQueue queue, const std::vector<VkSemaphore>& waitSemaphores = {}, const std::vector<VkPipelineStageFlags>& waitStages = {}, const std::vector<VkSemaphore>& signalSemaphores = {}, VkFence fence = VK_NULL_HANDLE) const;

  VkCommandBufferLevel         bufferLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  std::weak_ptr<CommandPool>   commandPool;
  VkDevice                     device = VK_NULL_HANDLE;
protected:
  std::vector<VkCommandBuffer>   commandBuffer;
  std::vector<char>              valid;
  mutable std::mutex             mutex;
  std::set<CommandBufferSource*> sources;
  uint32_t                       activeIndex   = 0;
};

void     CommandBuffer::setActiveIndex(uint32_t index) { activeIndex = index % commandBuffer.size(); }
uint32_t CommandBuffer::getActiveIndex() const         { return activeIndex; }
bool     CommandBuffer::isValid()                      { return valid[activeIndex]; }

// helper class defining pipeline barrier used later in CommandBuffer::cmdPipelineBarrier()
struct PUMEX_EXPORT PipelineBarrier
{
  PipelineBarrier() = delete;
  // ordinary memory barrier
  explicit PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask);
  // buffer barrier
  explicit PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);
  explicit PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkDescriptorBufferInfo bufferInfo);
  // image barrier
  explicit PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkImage image, VkImageSubresourceRange subresourceRange, VkImageLayout oldLayout, VkImageLayout newLayout );

  enum Type { Undefined, Memory, Image, Buffer };
  Type mType = Undefined;
  union
  {
    VkMemoryBarrier       memoryBarrier;
    VkBufferMemoryBarrier bufferBarrier;
    VkImageMemoryBarrier  imageBarrier;
  };
};

// Some classes used by CommandBuffer may change their internal values so that the CommandBuffer must be rebuilt
// Such classes should inherit from CommandBufferSource

class PUMEX_EXPORT CommandBufferSource : public std::enable_shared_from_this<CommandBufferSource>
{
public:
  virtual ~CommandBufferSource();

  void addCommandBuffer(CommandBuffer* commandBuffer);
  void removeCommandBuffer(CommandBuffer* commandBuffer);
  void notifyCommandBuffers(uint32_t index = UINT32_MAX);

protected:
  mutable std::mutex       commandMutex;
  std::set<CommandBuffer*> commandBuffers;
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

inline VkClearValue makeColorClearValue(const glm::vec4& color)
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

inline VkClearValue makeClearValue(const glm::vec4& color, VkImageAspectFlags aspectMask)
{
  if (aspectMask | VK_IMAGE_ASPECT_COLOR_BIT)
    return makeColorClearValue(color);
  return makeDepthStencilClearValue(color.x, color.y);
}

}