#include <pumex/Command.h>
#include <pumex/RenderPass.h>
#include <pumex/Device.h>
#include <pumex/Pipeline.h>
#include <pumex/utils/Log.h>

namespace pumex
{

CommandPool::CommandPool(uint32_t qfi)
  : queueFamilyIndex{qfi}
{
}

CommandPool::~CommandPool()
{
  for (auto& pddit : perDeviceData)
    vkDestroyCommandPool(pddit.first, pddit.second.commandPool, nullptr);
}


void CommandPool::validate(std::shared_ptr<pumex::Device> device)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit != perDeviceData.end())
    return;
  pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;

  VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
    cmdPoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VK_CHECK_LOG_THROW(vkCreateCommandPool(pddit->first, &cmdPoolInfo, nullptr, &pddit->second.commandPool), "Could not create command pool");
}

VkCommandPool CommandPool::getHandle(VkDevice device) const
{
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.commandPool;
}
CommandBuffer::CommandBuffer(VkCommandBufferLevel bf, std::shared_ptr<Device> d, std::shared_ptr<CommandPool> cp, uint32_t cbc)
  : bufferLevel{ bf }, commandPool{ cp }, device{d->device}
{
  commandBuffer.resize(cbc);
  VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
    cmdBufAllocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocateInfo.commandPool        = commandPool->getHandle(device);
    cmdBufAllocateInfo.level              = bufferLevel;
    cmdBufAllocateInfo.commandBufferCount = cbc;
  VK_CHECK_LOG_THROW(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, commandBuffer.data()), "failed vkAllocateCommandBuffers");
}

CommandBuffer::~CommandBuffer()
{
  vkFreeCommandBuffers(device, commandPool->getHandle(device), commandBuffer.size(), commandBuffer.data());
}


VkCommandBuffer CommandBuffer::getHandle() const
{
  return commandBuffer[activeIndex];
}

void CommandBuffer::cmdBegin(VkCommandBufferUsageFlags usageFlags) const
{
  VkCommandBufferBeginInfo cmdBufInfo{};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufInfo.flags = usageFlags;
  VK_CHECK_LOG_THROW(vkBeginCommandBuffer(commandBuffer[activeIndex], &cmdBufInfo), "failed vkBeginCommandBuffer");
}

void CommandBuffer::cmdEnd() const
{
  VK_CHECK_LOG_THROW(vkEndCommandBuffer(commandBuffer[activeIndex]), "failed vkEndCommandBuffer");
}

void CommandBuffer::cmdBeginRenderPass(std::shared_ptr<pumex::RenderPass> renderPass, VkFramebuffer frameBuffer, VkRect2D renderArea, const std::vector<VkClearValue>& clearValues) const
{
  VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass      = renderPass->getHandle(device);
    renderPassBeginInfo.renderArea      = renderArea;
    renderPassBeginInfo.clearValueCount = clearValues.size();;
    renderPassBeginInfo.pClearValues    = clearValues.data();
    renderPassBeginInfo.framebuffer     = frameBuffer;
  vkCmdBeginRenderPass(commandBuffer[activeIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void CommandBuffer::cmdEndRenderPass() const
{
  vkCmdEndRenderPass(commandBuffer[activeIndex]);
}




void CommandBuffer::cmdSetViewport(uint32_t firstViewport, const std::vector<VkViewport> viewports) const
{
  vkCmdSetViewport(commandBuffer[activeIndex], firstViewport, viewports.size(), viewports.data());
}

void CommandBuffer::cmdSetScissor(uint32_t firstScissor, const std::vector<VkRect2D> scissors) const
{
  vkCmdSetScissor(commandBuffer[activeIndex], firstScissor, scissors.size(), scissors.data());
}


void CommandBuffer::cmdPipelineBarrier(VkPipelineStageFlagBits srcStageMask, VkPipelineStageFlagBits dstStageMask, VkDependencyFlags dependencyFlags, const std::vector<PipelineBarrier>& barriers) const
{
  std::vector<VkMemoryBarrier>       memoryBarriers;
  std::vector<VkBufferMemoryBarrier> bufferBarriers;
  std::vector<VkImageMemoryBarrier>  imageBarriers;

  for (const auto& b : barriers)
  {
    switch (b.mType)
    {
    case PipelineBarrier::Memory:
      memoryBarriers.push_back(b.memoryBarrier);
      break;
    case PipelineBarrier::Buffer:
      bufferBarriers.push_back(b.bufferBarrier);
      break;
    case PipelineBarrier::Image:
      imageBarriers.push_back(b.imageBarrier);
      break;
    }
  }
  vkCmdPipelineBarrier(commandBuffer[activeIndex], srcStageMask, dstStageMask, dependencyFlags,
    memoryBarriers.size(), memoryBarriers.data(), bufferBarriers.size(), bufferBarriers.data(), imageBarriers.size(), imageBarriers.data());
}

void CommandBuffer::cmdPipelineBarrier(VkPipelineStageFlagBits srcStageMask, VkPipelineStageFlagBits dstStageMask, VkDependencyFlags dependencyFlags, const PipelineBarrier& barrier) const
{
  switch (barrier.mType)
  {
  case PipelineBarrier::Memory:
    vkCmdPipelineBarrier(commandBuffer[activeIndex], srcStageMask, dstStageMask, dependencyFlags, 1, &barrier.memoryBarrier, 0, nullptr, 0, nullptr);
    break;
  case PipelineBarrier::Buffer:
    vkCmdPipelineBarrier(commandBuffer[activeIndex], srcStageMask, dstStageMask, dependencyFlags, 0, nullptr, 1, &barrier.bufferBarrier, 0, nullptr);
    break;
  case PipelineBarrier::Image:
    vkCmdPipelineBarrier(commandBuffer[activeIndex], srcStageMask, dstStageMask, dependencyFlags, 0, nullptr, 0, nullptr, 1, &barrier.imageBarrier );
    break;
  }
}

void CommandBuffer::cmdCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, std::vector<VkBufferCopy> bufferCopy) const
{
  vkCmdCopyBuffer(commandBuffer[activeIndex], srcBuffer, dstBuffer, bufferCopy.size(), bufferCopy.data());
}

void CommandBuffer::cmdCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, const VkBufferCopy& bufferCopy) const
{
  vkCmdCopyBuffer(commandBuffer[activeIndex], srcBuffer, dstBuffer, 1, &bufferCopy);
}

void CommandBuffer::cmdBindPipeline(std::shared_ptr<pumex::ComputePipeline> pipeline) const
{
  vkCmdBindPipeline(commandBuffer[activeIndex], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getHandle(device));
}

void CommandBuffer::cmdBindPipeline(std::shared_ptr<pumex::GraphicsPipeline> pipeline) const
{
  vkCmdBindPipeline(commandBuffer[activeIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getHandle(device));
}

void CommandBuffer::cmdBindDescriptorSets(VkPipelineBindPoint bindPoint, std::shared_ptr<pumex::PipelineLayout> pipelineLayout, uint32_t firstSet, const std::vector<std::shared_ptr<pumex::DescriptorSet>> descriptorSets) const
{
  std::vector<VkDescriptorSet> descSets;
  for (const auto& d : descriptorSets)
    descSets.push_back(d->getHandle(device));
  // TODO : dynamic offset counts
  vkCmdBindDescriptorSets(commandBuffer[activeIndex], bindPoint, pipelineLayout->getHandle(device), firstSet, descSets.size(), descSets.data(), 0, nullptr);
}

void CommandBuffer::cmdBindDescriptorSets(VkPipelineBindPoint bindPoint, std::shared_ptr<pumex::PipelineLayout> pipelineLayout, uint32_t firstSet, std::shared_ptr<pumex::DescriptorSet> descriptorSet) const
{
  VkDescriptorSet descSet = descriptorSet->getHandle(device);
  // TODO : dynamic offset counts
  vkCmdBindDescriptorSets(commandBuffer[activeIndex], bindPoint, pipelineLayout->getHandle(device), firstSet, 1, &descSet, 0, nullptr);
}

void CommandBuffer::cmdDrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) const
{
  vkCmdDrawIndexed(commandBuffer[activeIndex], indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void CommandBuffer::cmdDrawIndexedIndirect(VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) const
{
  vkCmdDrawIndexedIndirect(commandBuffer[activeIndex], buffer, offset, drawCount, stride);
}

void CommandBuffer::cmdDispatch(uint32_t x, uint32_t y, uint32_t z) const
{
  vkCmdDispatch(commandBuffer[activeIndex], x, y, z);
}


void CommandBuffer::queueSubmit(VkQueue queue, const std::vector<VkSemaphore>& waitSemaphores, const std::vector<VkPipelineStageFlags>& waitStages, const std::vector<VkSemaphore>& signalSemaphores, VkFence fence ) const
{
  VkPipelineStageFlags stages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = waitSemaphores.size();
    submitInfo.pWaitSemaphores      = waitSemaphores.data();
    submitInfo.pWaitDstStageMask    = waitStages.data();
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &commandBuffer[activeIndex];
    submitInfo.signalSemaphoreCount = signalSemaphores.size();
    submitInfo.pSignalSemaphores    = signalSemaphores.data();
  VK_CHECK_LOG_THROW(vkQueueSubmit(queue, 1, &submitInfo, fence), "failed vkQueueSubmit");
}

PipelineBarrier::PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask)
  : mType(Memory)
{
  memoryBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  memoryBarrier.pNext         = nullptr;
  memoryBarrier.srcAccessMask = srcAccessMask;
  memoryBarrier.dstAccessMask = dstAccessMask;
}

PipelineBarrier::PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
  : mType(Buffer)
{
  bufferBarrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  bufferBarrier.pNext               = nullptr;
  bufferBarrier.srcAccessMask       = srcAccessMask;
  bufferBarrier.dstAccessMask       = dstAccessMask;
  bufferBarrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
  bufferBarrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
  bufferBarrier.buffer              = buffer;
  bufferBarrier.offset              = offset;
  bufferBarrier.size                = size;
}

PipelineBarrier::PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkDescriptorBufferInfo bufferInfo)
  : mType(Buffer)
{
  bufferBarrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  bufferBarrier.pNext               = nullptr;
  bufferBarrier.srcAccessMask       = srcAccessMask;
  bufferBarrier.dstAccessMask       = dstAccessMask;
  bufferBarrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
  bufferBarrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
  bufferBarrier.buffer              = bufferInfo.buffer;
  bufferBarrier.offset              = bufferInfo.offset;
  bufferBarrier.size                = bufferInfo.range;
}


PipelineBarrier::PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkImage image, VkImageSubresourceRange subresourceRange)
  : mType(Image)
{
  imageBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  imageBarrier.pNext               = nullptr;
  imageBarrier.srcAccessMask       = srcAccessMask;
  imageBarrier.dstAccessMask       = dstAccessMask;
  imageBarrier.oldLayout           = oldLayout;
  imageBarrier.newLayout           = newLayout;
  imageBarrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
  imageBarrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
  imageBarrier.image               = image;
  imageBarrier.subresourceRange    = subresourceRange;
}

}