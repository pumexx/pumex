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

#include <pumex/Command.h>
#include <pumex/RenderPass.h>
#include <pumex/RenderContext.h>
#include <pumex/FrameBuffer.h>
#include <pumex/Device.h>
#include <pumex/Surface.h>
#include <pumex/Descriptor.h>
#include <pumex/Pipeline.h>
#include <pumex/MemoryObjectBarrier.h>
#include <pumex/utils/Log.h>

using namespace pumex;

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
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  if (pddit != end(perDeviceData))
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
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == end(perDeviceData))
    return VK_NULL_HANDLE;
  return pddit->second.commandPool;
}

CommandBuffer::CommandBuffer(VkCommandBufferLevel bf, Device* d, std::shared_ptr<CommandPool> cp, uint32_t cbc)
  : bufferLevel{ bf }, commandPool{ cp }, device{ d->device }
{
  commandBuffer.resize(cbc);
  VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
    cmdBufAllocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocateInfo.commandPool        = commandPool.lock()->getHandle(device);
    cmdBufAllocateInfo.level              = bufferLevel;
    cmdBufAllocateInfo.commandBufferCount = cbc;
  VK_CHECK_LOG_THROW(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, commandBuffer.data()), "failed vkAllocateCommandBuffers");
  valid.resize(cbc, false);
}

CommandBuffer::~CommandBuffer()
{
//  clearSources();
  vkFreeCommandBuffers(device, commandPool.lock()->getHandle(device), commandBuffer.size(), commandBuffer.data());
}

void CommandBuffer::invalidate(uint32_t index)
{
  if (index == std::numeric_limits<uint32_t>::max())
    std::fill(begin(valid), end(valid), false);
  else
    valid[index % commandBuffer.size()] = false;
}

void CommandBuffer::addSource(CommandBufferSource* source)
{
  std::lock_guard<std::mutex> lock(mutex);
  sources.insert(source);
  source->addCommandBuffer(this);
}

void CommandBuffer::clearSources()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& s : sources)
    s->removeCommandBuffer(this);
  sources.clear();
}

VkCommandBuffer CommandBuffer::getHandle() const
{
  return commandBuffer[activeIndex];
}

void CommandBuffer::cmdBegin(VkCommandBufferUsageFlags usageFlags, VkRenderPass renderPass, uint32_t subPass)
{
  clearSources();
  VkCommandBufferBeginInfo cmdBufInfo{};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufInfo.flags = usageFlags;
  VkCommandBufferInheritanceInfo inheritanceInfo{};
  if (bufferLevel == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
  {
    inheritanceInfo.sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritanceInfo.renderPass  = renderPass;
    inheritanceInfo.subpass     = subPass;
    cmdBufInfo.pInheritanceInfo = &inheritanceInfo;
  }
  VK_CHECK_LOG_THROW(vkBeginCommandBuffer(commandBuffer[activeIndex], &cmdBufInfo), "failed vkBeginCommandBuffer");
}

void CommandBuffer::cmdEnd()
{
  VK_CHECK_LOG_THROW(vkEndCommandBuffer(commandBuffer[activeIndex]), "failed vkEndCommandBuffer");
  valid[activeIndex] = true;
}

void CommandBuffer::cmdBeginRenderPass(const RenderContext& renderContext, RenderSubPass* renderSubPass, VkRect2D renderArea, const std::vector<VkClearValue>& clearValues, VkSubpassContents subpassContents)
{
  addSource(renderSubPass);
  addSource(renderContext.frameBuffer.get());
  VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass      = renderSubPass->renderPass->getHandle(renderContext);
    renderPassBeginInfo.renderArea      = renderArea;
    renderPassBeginInfo.clearValueCount = clearValues.size();
    renderPassBeginInfo.pClearValues    = clearValues.data();
    renderPassBeginInfo.framebuffer     = renderContext.frameBuffer->getHandleFrameBuffer(renderContext);
  vkCmdBeginRenderPass(commandBuffer[renderContext.activeIndex], &renderPassBeginInfo, subpassContents);
}

void CommandBuffer::cmdNextSubPass(RenderSubPass* renderSubPass, VkSubpassContents contents)
{
  addSource(renderSubPass);
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

void CommandBuffer::cmdPipelineBarrier(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, const std::vector<PipelineBarrier>& barriers) const
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
    default:
      break;
    }
  }
  vkCmdPipelineBarrier(commandBuffer[activeIndex], srcStageMask, dstStageMask, dependencyFlags,
    memoryBarriers.size(), memoryBarriers.data(), bufferBarriers.size(), bufferBarriers.data(), imageBarriers.size(), imageBarriers.data());
}

void CommandBuffer::cmdPipelineBarrier(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, const PipelineBarrier& barrier) const
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
  default:
    break;
  }
}

void CommandBuffer::cmdPipelineBarrier(const RenderContext& renderContext, const MemoryObjectBarrierGroup& barrierGroup, const std::vector<MemoryObjectBarrier>& barriers)
{
  std::vector<VkMemoryBarrier>       memoryBarriers;
  std::vector<VkBufferMemoryBarrier> bufferBarriers;
  std::vector<VkImageMemoryBarrier>  imageBarriers;

  for (const auto& b : barriers)
  {
    switch (b.objectType)
    {
    case MemoryObject::moBuffer:
    {
      std::shared_ptr<MemoryBuffer> memoryBuffer = std::dynamic_pointer_cast<MemoryBuffer>(b.memoryObject);
      VkBufferMemoryBarrier bufferBarrier;
        bufferBarrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufferBarrier.pNext               = nullptr;
        bufferBarrier.srcAccessMask       = b.srcAccessMask;
        bufferBarrier.dstAccessMask       = b.dstAccessMask;
        bufferBarrier.srcQueueFamilyIndex = b.srcQueueFamilyIndex;
        bufferBarrier.dstQueueFamilyIndex = b.dstQueueFamilyIndex;
        bufferBarrier.buffer              = memoryBuffer->getHandleBuffer(renderContext);
        bufferBarrier.offset              = b.bufferRange.offset;
        bufferBarrier.size                = b.bufferRange.range;
      bufferBarriers.emplace_back(bufferBarrier);
      break;
    }
    case MemoryObject::moImage:
    {
      std::shared_ptr<MemoryImage> memoryImage = std::dynamic_pointer_cast<MemoryImage>(b.memoryObject);
      VkImageMemoryBarrier imageBarrier;
        imageBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarrier.pNext               = nullptr;
        imageBarrier.srcAccessMask       = b.srcAccessMask;
        imageBarrier.dstAccessMask       = b.dstAccessMask;
        imageBarrier.srcQueueFamilyIndex = b.srcQueueFamilyIndex;
        imageBarrier.dstQueueFamilyIndex = b.dstQueueFamilyIndex;
        imageBarrier.oldLayout           = b.oldLayout;
        imageBarrier.newLayout           = b.newLayout;
        imageBarrier.image               = memoryImage->getImage(renderContext)->getHandleImage();
        imageBarrier.subresourceRange    = b.imageRange.getSubresource();
      imageBarriers.emplace_back(imageBarrier);
      break;
    }
    default:
      break;
    }
  }
  vkCmdPipelineBarrier(commandBuffer[activeIndex], barrierGroup.srcStageMask, barrierGroup.dstStageMask, barrierGroup.dependencyFlags,
    memoryBarriers.size(), memoryBarriers.data(), bufferBarriers.size(), bufferBarriers.data(), imageBarriers.size(), imageBarriers.data());
}

void CommandBuffer::cmdCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, std::vector<VkBufferCopy> bufferCopy) const
{
  vkCmdCopyBuffer(commandBuffer[activeIndex], srcBuffer, dstBuffer, bufferCopy.size(), bufferCopy.data());
}

void CommandBuffer::cmdCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, const VkBufferCopy& bufferCopy) const
{
  vkCmdCopyBuffer(commandBuffer[activeIndex], srcBuffer, dstBuffer, 1, &bufferCopy);
}

void CommandBuffer::cmdBindPipeline(const RenderContext& renderContext, ComputePipeline* pipeline)
{
  addSource(pipeline);
  vkCmdBindPipeline(commandBuffer[activeIndex], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getHandlePipeline(renderContext));
}

void CommandBuffer::cmdBindPipeline(const RenderContext& renderContext, GraphicsPipeline* pipeline)
{
  addSource(pipeline);
  vkCmdBindPipeline(commandBuffer[activeIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getHandlePipeline(renderContext));
}

void CommandBuffer::cmdBindDescriptorSets(const RenderContext& renderContext, PipelineLayout* pipelineLayout, uint32_t firstSet, const std::vector<DescriptorSet*> descriptorSets)
{
  std::vector<VkDescriptorSet> descSets;
  for (auto& d : descriptorSets)
  {
    addSource(d);
    descSets.push_back(d->getHandle(renderContext));
  }
  // TODO : dynamic offset counts
  vkCmdBindDescriptorSets(commandBuffer[activeIndex], renderContext.currentBindPoint, pipelineLayout->getHandle(device), firstSet, descSets.size(), descSets.data(), 0, nullptr);
}

void CommandBuffer::cmdBindDescriptorSets(const RenderContext& renderContext, PipelineLayout* pipelineLayout, uint32_t firstSet, DescriptorSet* descriptorSet)
{
  addSource(descriptorSet);
  VkDescriptorSet descSet = descriptorSet->getHandle(renderContext);
  // TODO : dynamic offset counts
  vkCmdBindDescriptorSets(commandBuffer[activeIndex], renderContext.currentBindPoint, pipelineLayout->getHandle(device), firstSet, 1, &descSet, 0, nullptr);
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
  vkCmdCopyBufferToImage(commandBuffer[activeIndex], srcBuffer, image.getHandleImage(), dstImageLayout, regions.size(), regions.data());
}

void CommandBuffer::cmdClearColorImage(const Image& image, VkImageLayout imageLayout, VkClearValue color, std::vector<VkImageSubresourceRange> subresourceRanges)
{
  vkCmdClearColorImage(commandBuffer[activeIndex], image.getHandleImage(), imageLayout, &color.color, subresourceRanges.size(), subresourceRanges.data());
}

void CommandBuffer::cmdClearDepthStencilImage(const Image& image, VkImageLayout imageLayout, VkClearValue depthStencil, std::vector<VkImageSubresourceRange> subresourceRanges)
{
  vkCmdClearDepthStencilImage(commandBuffer[activeIndex], image.getHandleImage(), imageLayout, &depthStencil.depthStencil, subresourceRanges.size(), subresourceRanges.data());
}

void CommandBuffer::setImageLayout(Image& image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkImageSubresourceRange subresourceRange) const
{
  // Source access mask controls actions that have to be finished on the old layout
  // before it will be transitioned to the new layout
  VkAccessFlags srcAccessMask = 0;
  VkAccessFlags dstAccessMask = 0;
  VkPipelineStageFlags srcStageFlags  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dstStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

  switch (oldImageLayout)
  {
  case VK_IMAGE_LAYOUT_GENERAL:
    // actually we don't know the usage of the image
    srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    srcStageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    break;

  case VK_IMAGE_LAYOUT_UNDEFINED:
    // Image layout is undefined (or does not matter)
    // Only valid as initial layout
    // No flags required, listed only for completeness
    srcAccessMask = 0;
    srcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    break;
  case VK_IMAGE_LAYOUT_PREINITIALIZED:
    // Image is preinitialized
    // Only valid as initial layout for linear images, preserves memory contents
    // Make sure host writes have been finished
    srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    srcStageFlags = VK_PIPELINE_STAGE_HOST_BIT;
    break;
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    // Image is a color attachment
    // Make sure any writes to the color buffer have been finished
    srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    srcStageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    break;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    // Image is a depth/stencil attachment
    // Make sure any writes to the depth/stencil buffer have been finished
    srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    srcStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    // Image is a transfer source
    // Make sure any reads from the image have been finished
    srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    srcStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    // Image is a transfer destination
    // Make sure any writes to the image have been finished
    srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
    break;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    // Image is read by a shader
    // Make sure any shader reads from the image have been finished
    srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStageFlags = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    break;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
    srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    srcStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    break;
  default:
    srcAccessMask = 0;
    srcStageFlags = 0;
    break;
  }

  // Target layouts (new)
  // Destination access mask controls the dependency for the new image layout
  switch (newImageLayout)
  {
  case VK_IMAGE_LAYOUT_GENERAL:
    // actually we don't know the usage of the image
    dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    dstStageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    // Image will be used as a transfer destination
    // Make sure any writes to the image have been finished
    dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    // Image will be used as a transfer source
    // Make sure any reads from and writes to the image have been finished
    srcAccessMask = srcAccessMask | VK_ACCESS_TRANSFER_READ_BIT;
    srcStageFlags = srcStageFlags | VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    dstStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
    break;
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    // Image will be used as a color attachment
    // Make sure any writes to the color buffer have been finished
    srcAccessMask = srcAccessMask | VK_ACCESS_TRANSFER_READ_BIT;
    srcStageFlags = srcStageFlags | VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dstStageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    break;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    // Image layout will be used as a depth/stencil attachment
    // Make sure any writes to depth/stencil buffer have been finished
    dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dstStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    break;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    // Image will be read in a shader (sampler, input attachment)
    // Make sure any writes to the image have been finished
    if (srcAccessMask == 0)
      srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dstStageFlags = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
    if (srcAccessMask == 0)
      srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    dstStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    break;
  default:
    dstAccessMask = 0;
    dstStageFlags = 0;
    break;

  }

  VkDependencyFlags dependencyFlags = 0;
  cmdPipelineBarrier(srcStageFlags, dstStageFlags, dependencyFlags, PipelineBarrier(srcAccessMask, dstAccessMask, 0, 0, image.getHandleImage(), subresourceRange, oldImageLayout, newImageLayout ));
//  image.setImageLayout(newImageLayout);
}

void CommandBuffer::setImageLayout(Image& image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout) const
{
  VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask     = aspectMask;
    subresourceRange.baseMipLevel   = 0;
    subresourceRange.levelCount     = image.getImageTraits().mipLevels;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount     = image.getImageTraits().arrayLayers;
  setImageLayout(image, aspectMask, oldImageLayout, newImageLayout, subresourceRange);
}

void CommandBuffer::executeCommandBuffer(const RenderContext& renderContext, CommandBuffer* secondaryBuffer)
{
  VkCommandBuffer secBuffer = secondaryBuffer->getHandle();
  vkCmdExecuteCommands(commandBuffer[activeIndex], 1, &secBuffer);
}

void CommandBuffer::queueSubmit(VkQueue queue, const std::vector<VkSemaphore>& waitSemaphores, const std::vector<VkPipelineStageFlags>& waitStages, const std::vector<VkSemaphore>& signalSemaphores, VkFence fence ) const
{
  std::lock_guard<std::mutex> lock(mutex);
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
  : mType{ Memory }
{
  memoryBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  memoryBarrier.pNext         = nullptr;
  memoryBarrier.srcAccessMask = srcAccessMask;
  memoryBarrier.dstAccessMask = dstAccessMask;
}

PipelineBarrier::PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
  : mType{ Buffer }
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
  : mType{ Buffer }
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

PipelineBarrier::PipelineBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkImage image, VkImageSubresourceRange subresourceRange, VkImageLayout oldLayout, VkImageLayout newLayout)
  : mType{ Image }
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

CommandBufferSource::~CommandBufferSource()
{
}

void CommandBufferSource::addCommandBuffer(CommandBuffer* commandBuffer)
{
  std::lock_guard<std::mutex> lock(commandMutex);
  commandBuffers.insert(commandBuffer);
}

void CommandBufferSource::removeCommandBuffer(CommandBuffer* commandBuffer)
{
  std::lock_guard<std::mutex> lock(commandMutex);
  commandBuffers.erase(commandBuffer);
}

void CommandBufferSource::notifyCommandBuffers(uint32_t index)
{
  std::lock_guard<std::mutex> lock(commandMutex);
  for (auto cb : commandBuffers)
    cb->invalidate(index);
}
