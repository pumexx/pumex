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
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (!pddit->second.dirty)
    return;
  // no updates to earlier created command pool - just creation
  if (pddit->second.commandPool != VK_NULL_HANDLE)
    return;

  VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VK_CHECK_LOG_THROW(vkCreateCommandPool(pddit->first, &cmdPoolInfo, nullptr, &pddit->second.commandPool), "Could not create command pool");
  pddit->second.dirty = false;
}

VkCommandPool CommandPool::getHandle(VkDevice device) const
{
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.commandPool;
}

void CommandPool::setDirty()
{
  for (auto& pdd : perDeviceData)
    pdd.second.dirty = true;
}


CommandBuffer::CommandBuffer(VkCommandBufferLevel bf, std::shared_ptr<CommandPool> cp)
  : bufferLevel{ bf }, commandPool{ cp }
{
}

CommandBuffer::~CommandBuffer()
{
  for (auto& pddit : perDeviceData)
    vkFreeCommandBuffers(pddit.first, commandPool->getHandle(pddit.first),1, &pddit.second.commandBuffer);
}


void CommandBuffer::validate(std::shared_ptr<pumex::Device> device)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (!pddit->second.dirty)
    return;
  // no updates to earlier created command buffer - just creation
  if (pddit->second.commandBuffer != VK_NULL_HANDLE)
    return;

  VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
    cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocateInfo.commandPool = commandPool->getHandle(pddit->first);
    cmdBufAllocateInfo.level       = bufferLevel;
    cmdBufAllocateInfo.commandBufferCount = 1;
  VK_CHECK_LOG_THROW(vkAllocateCommandBuffers(pddit->first, &cmdBufAllocateInfo, &pddit->second.commandBuffer), "failed vkAllocateCommandBuffers");
  pddit->second.dirty = false;
}

VkCommandBuffer CommandBuffer::getHandle(VkDevice device) const
{
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.commandBuffer;
}

void CommandBuffer::setDirty()
{
  for (auto& pdd : perDeviceData)
    pdd.second.dirty = true;
}

void CommandBuffer::cmdBegin(std::shared_ptr<pumex::Device> device, VkCommandBufferUsageFlags usageFlags) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  VkCommandBufferBeginInfo cmdBufInfo{};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufInfo.flags = usageFlags;
  VK_CHECK_LOG_THROW(vkBeginCommandBuffer(pddit->second.commandBuffer, &cmdBufInfo), "failed vkBeginCommandBuffer");
}

void CommandBuffer::cmdEnd(std::shared_ptr<pumex::Device> device) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  VK_CHECK_LOG_THROW(vkEndCommandBuffer(pddit->second.commandBuffer), "failed vkEndCommandBuffer");
}

void CommandBuffer::cmdBeginRenderPass(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::RenderPass> renderPass, VkFramebuffer frameBuffer, VkRect2D renderArea, const std::vector<VkClearValue>& clearValues) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;

  VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderPass->getHandle(pddit->first);
    renderPassBeginInfo.renderArea = renderArea;
    renderPassBeginInfo.clearValueCount = clearValues.size();;
    renderPassBeginInfo.pClearValues = clearValues.data();
    renderPassBeginInfo.framebuffer = frameBuffer;
  vkCmdBeginRenderPass(pddit->second.commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void CommandBuffer::cmdEndRenderPass(std::shared_ptr<pumex::Device> device) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  vkCmdEndRenderPass(pddit->second.commandBuffer);
}




void CommandBuffer::cmdSetViewport(std::shared_ptr<pumex::Device> device, uint32_t firstViewport, const std::vector<VkViewport> viewports) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  vkCmdSetViewport(pddit->second.commandBuffer, firstViewport, viewports.size(), viewports.data());
}

void CommandBuffer::cmdSetScissor(std::shared_ptr<pumex::Device> device, uint32_t firstScissor, const std::vector<VkRect2D> scissors) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  vkCmdSetScissor(pddit->second.commandBuffer, firstScissor, scissors.size(), scissors.data());
}


void CommandBuffer::cmdPipelineBarrier(std::shared_ptr<pumex::Device> device, VkPipelineStageFlagBits srcStageMask, VkPipelineStageFlagBits dstStageMask, VkDependencyFlags dependencyFlags, const std::vector<PipelineBarrier>& barriers) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
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
  vkCmdPipelineBarrier(pddit->second.commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
    memoryBarriers.size(), memoryBarriers.data(), bufferBarriers.size(), bufferBarriers.data(), imageBarriers.size(), imageBarriers.data());
}

void CommandBuffer::cmdPipelineBarrier(std::shared_ptr<pumex::Device> device, VkPipelineStageFlagBits srcStageMask, VkPipelineStageFlagBits dstStageMask, VkDependencyFlags dependencyFlags, const PipelineBarrier& barrier) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  switch (barrier.mType)
  {
  case PipelineBarrier::Memory:
    vkCmdPipelineBarrier(pddit->second.commandBuffer, srcStageMask, dstStageMask, dependencyFlags, 1, &barrier.memoryBarrier, 0, nullptr, 0, nullptr);
    break;
  case PipelineBarrier::Buffer:
    vkCmdPipelineBarrier(pddit->second.commandBuffer, srcStageMask, dstStageMask, dependencyFlags, 0, nullptr, 1, &barrier.bufferBarrier, 0, nullptr);
    break;
  case PipelineBarrier::Image:
    vkCmdPipelineBarrier(pddit->second.commandBuffer, srcStageMask, dstStageMask, dependencyFlags, 0, nullptr, 0, nullptr, 1, &barrier.imageBarrier );
    break;
  }
}

void CommandBuffer::cmdCopyBuffer(std::shared_ptr<pumex::Device> device, VkBuffer srcBuffer, VkBuffer dstBuffer, std::vector<VkBufferCopy> bufferCopy) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  vkCmdCopyBuffer(pddit->second.commandBuffer, srcBuffer, dstBuffer, bufferCopy.size(), bufferCopy.data());

}

void CommandBuffer::cmdCopyBuffer(std::shared_ptr<pumex::Device> device, VkBuffer srcBuffer, VkBuffer dstBuffer, const VkBufferCopy& bufferCopy) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  vkCmdCopyBuffer(pddit->second.commandBuffer, srcBuffer, dstBuffer, 1, &bufferCopy);
}

void CommandBuffer::cmdBindPipeline(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::ComputePipeline> pipeline) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  vkCmdBindPipeline(pddit->second.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getHandle(pddit->first));
}

void CommandBuffer::cmdBindPipeline(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::GraphicsPipeline> pipeline) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  vkCmdBindPipeline(pddit->second.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getHandle(pddit->first));
}

void CommandBuffer::cmdBindDescriptorSets(std::shared_ptr<pumex::Device> device, VkPipelineBindPoint bindPoint, std::shared_ptr<pumex::PipelineLayout> pipelineLayout, uint32_t firstSet, const std::vector<std::shared_ptr<pumex::DescriptorSet>> descriptorSets) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  std::vector<VkDescriptorSet> descSets;
  for (const auto& d : descriptorSets)
    descSets.push_back(d->getHandle(pddit->first));
  // TODO : dynamic offset counts
  vkCmdBindDescriptorSets(pddit->second.commandBuffer, bindPoint, pipelineLayout->getHandle(pddit->first), firstSet, descSets.size(), descSets.data(), 0, nullptr);
}

void CommandBuffer::cmdBindDescriptorSets(std::shared_ptr<pumex::Device> device, VkPipelineBindPoint bindPoint, std::shared_ptr<pumex::PipelineLayout> pipelineLayout, uint32_t firstSet, std::shared_ptr<pumex::DescriptorSet> descriptorSet) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  VkDescriptorSet descSet = descriptorSet->getHandle(pddit->first);
  // TODO : dynamic offset counts
  vkCmdBindDescriptorSets(pddit->second.commandBuffer, bindPoint, pipelineLayout->getHandle(pddit->first), firstSet, 1, &descSet, 0, nullptr);
}

void CommandBuffer::cmdDrawIndexed(std::shared_ptr<pumex::Device> device, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  vkCmdDrawIndexed(pddit->second.commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void CommandBuffer::cmdDrawIndexedIndirect(std::shared_ptr<pumex::Device> device, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  vkCmdDrawIndexedIndirect(pddit->second.commandBuffer, buffer, offset, drawCount, stride);
}

void CommandBuffer::cmdDispatch(std::shared_ptr<pumex::Device> device, uint32_t x, uint32_t y, uint32_t z) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;
  vkCmdDispatch(pddit->second.commandBuffer, x, y, z);
}




void CommandBuffer::queueSubmit(std::shared_ptr<Device> device, VkQueue queue, const std::vector<VkSemaphore>& waitSemaphores, const std::vector<VkPipelineStageFlags>& waitStages, const std::vector<VkSemaphore>& signalSemaphores, VkFence fence ) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    return;

  VkPipelineStageFlags stages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = waitSemaphores.size();
    submitInfo.pWaitSemaphores      = waitSemaphores.data();
    submitInfo.pWaitDstStageMask    = waitStages.data();
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &pddit->second.commandBuffer;
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