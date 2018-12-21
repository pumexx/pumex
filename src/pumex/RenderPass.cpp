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

#include <pumex/RenderPass.h>
#include <algorithm>
#include <sstream>
#include <iterator>
#include <pumex/Device.h>
#include <pumex/utils/Log.h>
#include <pumex/Surface.h>
#include <pumex/FrameBuffer.h>
#include <pumex/RenderVisitors.h>

using namespace pumex;

AttachmentDescription::AttachmentDescription(uint32_t id, VkFormat f, VkSampleCountFlagBits s, VkAttachmentLoadOp lop, VkAttachmentStoreOp sop, VkAttachmentLoadOp slop, VkAttachmentStoreOp ssop, VkImageLayout il, VkImageLayout fl, VkAttachmentDescriptionFlags fs)
  : imageDefinitionIndex{ id }, format{ f }, samples{ s }, loadOp { lop }, storeOp{ sop }, stencilLoadOp{ slop }, stencilStoreOp{ ssop }, initialLayout{ il }, finalLayout{ fl }, flags{ fs }
{
}

VkAttachmentDescription AttachmentDescription::getDescription() const
{
  VkAttachmentDescription desc{};
  desc.flags          = flags;
  desc.format         = format;
  desc.samples        = samples;
  desc.loadOp         = loadOp;
  desc.storeOp        = storeOp;
  desc.stencilLoadOp  = stencilLoadOp;
  desc.stencilStoreOp = stencilStoreOp;
  desc.initialLayout  = initialLayout;
  desc.finalLayout    = finalLayout;
  return desc;
}

AttachmentReference::AttachmentReference()
  : attachment{ VK_ATTACHMENT_UNUSED }, layout{ VK_IMAGE_LAYOUT_UNDEFINED }
{
}

AttachmentReference::AttachmentReference(uint32_t a, VkImageLayout l)
  : attachment{ a }, layout{ l }
{
}

VkAttachmentReference AttachmentReference::getReference() const
{
  VkAttachmentReference ref;
    ref.attachment = attachment;
    ref.layout     = layout;
  return ref;
}

SubpassDescription::SubpassDescription()
  : pipelineBindPoint{ VK_PIPELINE_BIND_POINT_GRAPHICS }, depthStencilAttachment{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED }, flags{ 0 }, multiViewMask{ 0x0 }
{
}

SubpassDescription::SubpassDescription(VkPipelineBindPoint pbp, const std::vector<AttachmentReference>& ia, const std::vector<AttachmentReference>& ca, const std::vector<AttachmentReference>& ra, const AttachmentReference& da, const std::vector<uint32_t>& pa, VkSubpassDescriptionFlags fs, uint32_t mvm)
  : pipelineBindPoint{ pbp }, preserveAttachments(pa), flags{ fs }, multiViewMask{ mvm }
{
  for ( const auto& a : ia)
    inputAttachments.emplace_back(a.getReference());
  for (const auto& a : ca)
    colorAttachments.emplace_back(a.getReference());
  for (const auto& a : ra)
    resolveAttachments.emplace_back(a.getReference());
  depthStencilAttachment = da.getReference();
}

// Be advised : resulting description is as long valid as SubpassDescription exists.
// We are passing pointers to internal elements here
VkSubpassDescription SubpassDescription::getDescription() const
{
  VkSubpassDescription desc;
    desc.flags                   = flags;
    desc.pipelineBindPoint       = pipelineBindPoint;
    desc.inputAttachmentCount    = inputAttachments.size();
    desc.pInputAttachments       = inputAttachments.data();
    desc.colorAttachmentCount    = colorAttachments.size();
    desc.pColorAttachments       = colorAttachments.data();
    desc.pResolveAttachments     = resolveAttachments.data();
    desc.pDepthStencilAttachment = &depthStencilAttachment;
    desc.preserveAttachmentCount = preserveAttachments.size();
    desc.pPreserveAttachments    = preserveAttachments.data();
  return desc;
}

SubpassDependencyDescription::SubpassDependencyDescription(uint32_t ss, uint32_t ds, VkPipelineStageFlags ssm, VkPipelineStageFlags dsm, VkAccessFlags sam, VkAccessFlags dam, VkDependencyFlags df)
  : srcSubpass{ ss }, dstSubpass{ ds }, srcStageMask{ ssm }, dstStageMask{ dsm }, srcAccessMask{ sam }, dstAccessMask{ dam }, dependencyFlags{df}
{
}

VkSubpassDependency SubpassDependencyDescription::getDependency() const
{
  VkSubpassDependency dep;
    dep.srcSubpass      = srcSubpass;
    dep.dstSubpass      = dstSubpass;
    dep.srcStageMask    = srcStageMask;
    dep.dstStageMask    = dstStageMask;
    dep.srcAccessMask   = srcAccessMask;
    dep.dstAccessMask   = dstAccessMask;
    dep.dependencyFlags = dependencyFlags;
  return dep;
}

RenderPass::RenderPass()
  : activeCount{ 1 }
{
}

RenderPass::~RenderPass()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
      vkDestroyRenderPass(pdd.second.device, pdd.second.data[i].renderPass, nullptr);
}

void RenderPass::addSubPass(std::shared_ptr<RenderSubPass> renderSubPass)
{
  std::lock_guard<std::mutex> lock(mutex);
  renderSubPass->renderPass = shared_from_this();
  renderSubPass->subpassIndex = subPasses.size();
  subPasses.push_back(renderSubPass);
}

void RenderPass::setRenderPassData(std::shared_ptr<FrameBuffer> fb, const std::vector<AttachmentDescription>& at, const std::vector<VkClearValue>& cv)
{
  std::lock_guard<std::mutex> lock(mutex);
  frameBuffer = fb;
  attachments = at;
  clearValues = cv;
  for( auto& pdd : perObjectData)
    pdd.second.invalidate();
}

void RenderPass::invalidate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(renderContext.vkDevice);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ renderContext.vkDevice, RenderPassData(renderContext, swForEachImage) }).first;
  pddit->second.invalidate();
}

void RenderPass::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }

  auto pddit = perObjectData.find(renderContext.vkDevice);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ renderContext.vkDevice, RenderPassData(renderContext, swForEachImage) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;
  if (pddit->second.data[activeIndex].renderPass != VK_NULL_HANDLE)
    vkDestroyRenderPass(pddit->first, pddit->second.data[activeIndex].renderPass, nullptr);

  std::vector<VkAttachmentDescription> attachmentDescriptions;
  for (const auto& ad : attachments)
    attachmentDescriptions.emplace_back(ad.getDescription());

  std::vector<VkSubpassDescription> subpassDescriptions;
  std::vector<uint32_t> multiViewMasks;
  for (auto& sp : subPasses)
  {
    subpassDescriptions.emplace_back(sp.lock()->definition.getDescription());
    multiViewMasks.push_back(sp.lock()->definition.multiViewMask);
  }

  std::vector<VkSubpassDependency> dependencyDescriptors;
  for (const auto& dp : dependencies)
    dependencyDescriptors.emplace_back(dp.getDependency());

  VkRenderPassCreateInfo renderPassCI{};
    renderPassCI.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCI.attachmentCount = attachmentDescriptions.size();
    renderPassCI.pAttachments    = attachmentDescriptions.data();
    renderPassCI.subpassCount    = subpassDescriptions.size();
    renderPassCI.pSubpasses      = subpassDescriptions.data();
    renderPassCI.dependencyCount = dependencyDescriptors.size();
    renderPassCI.pDependencies   = dependencyDescriptors.data();

  VkRenderPassMultiviewCreateInfo renderPassMultiviewCI{};
  if(multiViewRenderPass && renderContext.device->deviceExtensionEnabled(VK_KHR_MULTIVIEW_EXTENSION_NAME))
  {
    uint32_t correlationMask = 0x3U; // FIXME - hardcoded

    renderPassMultiviewCI.sType                = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
    renderPassMultiviewCI.subpassCount         = static_cast<uint32_t>(multiViewMasks.size());
    renderPassMultiviewCI.pViewMasks           = multiViewMasks.data();
    renderPassMultiviewCI.correlationMaskCount = 1;
    renderPassMultiviewCI.pCorrelationMasks    = &correlationMask;
    renderPassCI.pNext = &renderPassMultiviewCI;
  }
  VK_CHECK_LOG_THROW( vkCreateRenderPass(renderContext.vkDevice, &renderPassCI, nullptr, &pddit->second.data[activeIndex].renderPass), "Could not create render pass" );
  pddit->second.valid[activeIndex] = true;
}

VkRenderPass RenderPass::getHandle(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(renderContext.vkDevice);
  if (pddit == end(perObjectData))
    return VK_NULL_HANDLE;
  return pddit->second.data[renderContext.activeIndex % activeCount].renderPass;
}

RenderCommand::RenderCommand(RenderCommand::CommandType ct)
  : commandType{ ct }
{
}

std::shared_ptr<ImageView> RenderCommand::getImageViewByEntryName(const std::string& entryName) const
{
  auto it = entries.find(entryName);
  CHECK_LOG_THROW(it == end(entries), "No ImageView name = " << entryName);
  auto it2 = imageViews.find(it->second);
  CHECK_LOG_THROW(it2 == end(imageViews), "No ImageView name = " << entryName << " id = " << it->second);
  return it2->second;
}

std::shared_ptr<BufferView> RenderCommand::getBufferViewByEntryName(const std::string& entryName) const
{
  auto it = entries.find(entryName);
  CHECK_LOG_THROW(it == end(entries), "No BufferView name = " << entryName);
  auto it2 = bufferViews.find(it->second);
  CHECK_LOG_THROW(it2 == end(bufferViews), "No BufferView name = " << entryName << " id = " << it->second);
  return it2->second;
}

RenderSubPass::RenderSubPass()
  : RenderCommand(RenderCommand::ctRenderSubPass)
{
}

void RenderSubPass::setSubpassDescription(const SubpassDescription& SubpassDescription)
{
  definition = SubpassDescription;
}

void RenderSubPass::validate(const RenderContext& renderContext)
{
  renderPass->validate(renderContext);
}

void RenderSubPass::applyRenderContextVisitor(RenderContextVisitor& visitor)
{
  visitor.renderContext.setFrameBuffer(renderPass->frameBuffer);
  visitor.renderContext.setRenderPass(renderPass);
  visitor.renderContext.setSubpassIndex(subpassIndex);
  visitor.renderContext.setRenderOperation(&operation);

  operation.node->accept(visitor);

  visitor.renderContext.setRenderOperation(nullptr);
  visitor.renderContext.setSubpassIndex(0);
  visitor.renderContext.setRenderPass(nullptr);
  visitor.renderContext.setFrameBuffer(nullptr);
}

void RenderSubPass::buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor)
{
  commandVisitor.renderContext.setFrameBuffer(renderPass->frameBuffer);
  commandVisitor.renderContext.setRenderPass(renderPass);
  commandVisitor.renderContext.setSubpassIndex(subpassIndex);
  commandVisitor.renderContext.setRenderOperation(&operation);
  for (auto& barrierGroup : barriersBeforeOp)
    commandVisitor.commandBuffer->cmdPipelineBarrier(commandVisitor.renderContext, barrierGroup.first, barrierGroup.second);

  VkSubpassContents subpassContents = (operation.node.get() != nullptr && !operation.node->hasSecondaryBuffer()) ? VK_SUBPASS_CONTENTS_INLINE : VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS;
  if (subpassIndex == 0)
  {
    VkRect2D rectangle;
    VkViewport viewport;
    // FIXME : what about viewport Z coordinates ?
    switch (operation.attachmentSize.type)
    {
    case isSurfaceDependent:
      rectangle = makeVkRect2D(operation.attachmentSize, commandVisitor.renderContext.surface->swapChainSize);
      viewport  = makeVkViewport(0, 0, commandVisitor.renderContext.surface->swapChainSize.width * operation.attachmentSize.size.x, commandVisitor.renderContext.surface->swapChainSize.height * operation.attachmentSize.size.y, 0.0f, 1.0f);
      break;
    case isAbsolute:
      rectangle = makeVkRect2D(operation.attachmentSize);
      viewport  = makeVkViewport(0, 0, operation.attachmentSize.size.x, operation.attachmentSize.size.y, 0.0f, 1.0f);
      break;
    default:
      rectangle = makeVkRect2D(0, 0, 1, 1);
      viewport  = makeVkViewport(0, 0, 1, 1, 0.0f, 1.0f);
      break;
    }

    commandVisitor.commandBuffer->cmdBeginRenderPass( commandVisitor.renderContext, this, rectangle, renderPass->clearValues, subpassContents );
    commandVisitor.commandBuffer->cmdSetViewport(0, { viewport });
    commandVisitor.commandBuffer->cmdSetScissor(0, { rectangle });
  }
  else
  {
    commandVisitor.commandBuffer->cmdNextSubPass(this, subpassContents);
  }

  if (subpassContents == VK_SUBPASS_CONTENTS_INLINE)
    operation.node->accept(commandVisitor);
  else
    commandVisitor.commandBuffer->executeCommandBuffer(commandVisitor.renderContext, operation.node->getSecondaryBuffer(commandVisitor.renderContext).get());

  if (renderPass->subPasses.size() == subpassIndex + 1)
    commandVisitor.commandBuffer->cmdEndRenderPass();

  for (auto& barrierGroup : barriersAfterOp)
    commandVisitor.commandBuffer->cmdPipelineBarrier(commandVisitor.renderContext, barrierGroup.first, barrierGroup.second);

  commandVisitor.renderContext.setRenderOperation(nullptr);
  commandVisitor.renderContext.setSubpassIndex(0);
  commandVisitor.renderContext.setRenderPass(nullptr);
  commandVisitor.renderContext.setFrameBuffer(nullptr);
}

ComputePass::ComputePass()
  : RenderCommand(RenderCommand::ctComputePass)
{
}

void ComputePass::validate(const RenderContext& renderContext)
{
}

void ComputePass::applyRenderContextVisitor(RenderContextVisitor& visitor)
{
  visitor.renderContext.setRenderOperation(&operation);

  operation.node->accept(visitor);

  visitor.renderContext.setRenderOperation(nullptr);
}

void ComputePass::buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor)
{
  commandVisitor.renderContext.setRenderOperation(&operation);

  for (auto& barrierGroup : barriersBeforeOp)
    commandVisitor.commandBuffer->cmdPipelineBarrier(commandVisitor.renderContext, barrierGroup.first, barrierGroup.second);

  VkSubpassContents subpassContents = operation.node->hasSecondaryBuffer() ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE;
  if (subpassContents == VK_SUBPASS_CONTENTS_INLINE)
    operation.node->accept(commandVisitor);
  else
    commandVisitor.commandBuffer->executeCommandBuffer(commandVisitor.renderContext, operation.node->getSecondaryBuffer(commandVisitor.renderContext).get());

  for (auto& barrierGroup : barriersAfterOp)
    commandVisitor.commandBuffer->cmdPipelineBarrier(commandVisitor.renderContext, barrierGroup.first, barrierGroup.second);

  commandVisitor.renderContext.setRenderOperation(nullptr);
}

TransferPass::TransferPass()
  : RenderCommand(RenderCommand::ctTransferPass)

{
}

void TransferPass::validate(const RenderContext& renderContext)
{
}

void TransferPass::applyRenderContextVisitor(RenderContextVisitor& visitor)
{
  visitor.renderContext.setRenderOperation(&operation);

  operation.node->accept(visitor);

  visitor.renderContext.setRenderOperation(nullptr);
}

void TransferPass::buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor)
{
  commandVisitor.renderContext.setRenderOperation(&operation);

  for (auto& barrierGroup : barriersBeforeOp)
    commandVisitor.commandBuffer->cmdPipelineBarrier(commandVisitor.renderContext, barrierGroup.first, barrierGroup.second);

  VkSubpassContents subpassContents = operation.node->hasSecondaryBuffer() ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE;
  if (subpassContents == VK_SUBPASS_CONTENTS_INLINE)
    operation.node->accept(commandVisitor);
  else
    commandVisitor.commandBuffer->executeCommandBuffer(commandVisitor.renderContext, operation.node->getSecondaryBuffer(commandVisitor.renderContext).get());

  for (auto& barrierGroup : barriersAfterOp)
    commandVisitor.commandBuffer->cmdPipelineBarrier(commandVisitor.renderContext, barrierGroup.first, barrierGroup.second);

  commandVisitor.renderContext.setRenderOperation(nullptr);
}
