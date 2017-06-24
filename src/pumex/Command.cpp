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

#include <pumex/Command.h>
#include <pumex/RenderPass.h>
#include <pumex/Device.h>
#include <pumex/Pipeline.h>
#include <pumex/Texture.h>
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


void CommandPool::validate(Device* device)
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
CommandBuffer::CommandBuffer(VkCommandBufferLevel bf, Device* d, CommandPool* cp, uint32_t cbc)
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

void CommandBuffer::cmdBeginRenderPass(std::shared_ptr<RenderPass> renderPass, VkFramebuffer frameBuffer, VkRect2D renderArea, const std::vector<VkClearValue>& clearValues) const
{
  VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass      = renderPass->getHandle(device);
    renderPassBeginInfo.renderArea      = renderArea;
    renderPassBeginInfo.clearValueCount = clearValues.size();
    renderPassBeginInfo.pClearValues    = clearValues.data();
    renderPassBeginInfo.framebuffer     = frameBuffer;
  vkCmdBeginRenderPass(commandBuffer[activeIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void CommandBuffer::cmdNextSubPass(VkSubpassContents contents) const
{
  vkCmdNextSubpass(commandBuffer[activeIndex], contents);
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

void CommandBuffer::cmdBindPipeline(std::shared_ptr<ComputePipeline> pipeline) const
{
  vkCmdBindPipeline(commandBuffer[activeIndex], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getHandle(device));
}

void CommandBuffer::cmdBindPipeline(std::shared_ptr<GraphicsPipeline> pipeline) const
{
  vkCmdBindPipeline(commandBuffer[activeIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getHandle(device));
}

void CommandBuffer::cmdBindDescriptorSets(VkPipelineBindPoint bindPoint, VkSurfaceKHR surface, std::shared_ptr<PipelineLayout> pipelineLayout, uint32_t firstSet, const std::vector<std::shared_ptr<DescriptorSet>> descriptorSets) const
{
  std::vector<VkDescriptorSet> descSets;
  for (const auto& d : descriptorSets)
    descSets.push_back(d->getHandle(surface));
  // TODO : dynamic offset counts
  vkCmdBindDescriptorSets(commandBuffer[activeIndex], bindPoint, pipelineLayout->getHandle(device), firstSet, descSets.size(), descSets.data(), 0, nullptr);
}

void CommandBuffer::cmdBindDescriptorSets(VkPipelineBindPoint bindPoint, VkSurfaceKHR surface, std::shared_ptr<PipelineLayout> pipelineLayout, uint32_t firstSet, std::shared_ptr<DescriptorSet> descriptorSet) const
{
  VkDescriptorSet descSet = descriptorSet->getHandle(surface);
  // TODO : dynamic offset counts
  vkCmdBindDescriptorSets(commandBuffer[activeIndex], bindPoint, pipelineLayout->getHandle(device), firstSet, 1, &descSet, 0, nullptr);
}

void CommandBuffer::cmdDraw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t vertexOffset, uint32_t firstInstance) const
{
  vkCmdDraw(commandBuffer[activeIndex], vertexCount, instanceCount, firstVertex, firstInstance);
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

void CommandBuffer::cmdCopyBufferToImage(VkBuffer srcBuffer, const Image& image, VkImageLayout dstImageLayout, const std::vector<VkBufferImageCopy>& regions) const
{
  vkCmdCopyBufferToImage(commandBuffer[activeIndex], srcBuffer, image.getImage(), dstImageLayout, regions.size(), regions.data());
}


void CommandBuffer::setImageLayout(Image& image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkImageSubresourceRange subresourceRange) const
{
  // Source access mask controls actions that have to be finished on the old layout
  // before it will be transitioned to the new layout
  VkAccessFlags srcAccessMask, dstAccessMask;
  switch (oldImageLayout)
  {
  case VK_IMAGE_LAYOUT_UNDEFINED:
    // Image layout is undefined (or does not matter)
    // Only valid as initial layout
    // No flags required, listed only for completeness
    srcAccessMask = 0;
    break;
  case VK_IMAGE_LAYOUT_PREINITIALIZED:
    // Image is preinitialized
    // Only valid as initial layout for linear images, preserves memory contents
    // Make sure host writes have been finished
    srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    // Image is a color attachment
    // Make sure any writes to the color buffer have been finished
    srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    // Image is a depth/stencil attachment
    // Make sure any writes to the depth/stencil buffer have been finished
    srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    // Image is a transfer source 
    // Make sure any reads from the image have been finished
    srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    // Image is a transfer destination
    // Make sure any writes to the image have been finished
    srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    // Image is read by a shader
    // Make sure any shader reads from the image have been finished
    srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    break;
  }

  // Target layouts (new)
  // Destination access mask controls the dependency for the new image layout
  switch (newImageLayout)
  {
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    // Image will be used as a transfer destination
    // Make sure any writes to the image have been finished
    dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    // Image will be used as a transfer source
    // Make sure any reads from and writes to the image have been finished
    srcAccessMask = srcAccessMask | VK_ACCESS_TRANSFER_READ_BIT;
    dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    break;
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    // Image will be used as a color attachment
    // Make sure any writes to the color buffer have been finished
    srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    // Image layout will be used as a depth/stencil attachment
    // Make sure any writes to depth/stencil buffer have been finished
    dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    // Image will be read in a shader (sampler, input attachment)
    // Make sure any writes to the image have been finished
    if (srcAccessMask == 0)
      srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    break;
  }

  // Put barrier on top
  VkPipelineStageFlagBits srcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlagBits destStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkDependencyFlags dependencyFlags = 0;
  cmdPipelineBarrier(srcStageFlags, destStageFlags, dependencyFlags, PipelineBarrier(srcAccessMask, dstAccessMask, oldImageLayout, newImageLayout, 0, 0, image.getImage(), subresourceRange));
  image.setImageLayout(newImageLayout);
}

void CommandBuffer::setImageLayout(Image& image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout) const
{
  VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask   = aspectMask;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount   = image.getImageTraits().mipLevels;
    subresourceRange.layerCount   = image.getImageTraits().arrayLayers;
  setImageLayout(image, aspectMask, oldImageLayout, newImageLayout, subresourceRange);
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