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

#include <pumex/RenderPass.h>
#include <algorithm>
#include <sstream>
#include <iterator>
#include <pumex/Device.h>
#include <pumex/utils/Log.h>
#include <pumex/Surface.h>
#include <pumex/FrameBuffer.h>
#include <pumex/RenderWorkflow.h>
#include <pumex/RenderVisitors.h>

using namespace pumex;


AttachmentDefinition::AttachmentDefinition(uint32_t id, VkFormat f, VkSampleCountFlagBits s, VkAttachmentLoadOp lop, VkAttachmentStoreOp sop, VkAttachmentLoadOp slop, VkAttachmentStoreOp ssop, VkImageLayout il, VkImageLayout fl, VkAttachmentDescriptionFlags fs)
  : imageDefinitionIndex{ id }, format{ f }, samples{ s }, loadOp { lop }, storeOp{ sop }, stencilLoadOp{ slop }, stencilStoreOp{ ssop }, initialLayout{ il }, finalLayout{ fl }, flags{ fs }
{
}

VkAttachmentDescription AttachmentDefinition::getDescription() const
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

SubpassDefinition::SubpassDefinition(VkPipelineBindPoint pbp, const std::vector<AttachmentReference>& ia, const std::vector<AttachmentReference>& ca, const std::vector<AttachmentReference>& ra, const AttachmentReference& da, const std::vector<uint32_t>& pa, VkSubpassDescriptionFlags fs)
  : pipelineBindPoint{ pbp }, preserveAttachments(pa), flags{ fs }
{
  for ( const auto& a : ia)
    inputAttachments.emplace_back(a.getReference());
  for (const auto& a : ca)
    colorAttachments.emplace_back(a.getReference());
  for (const auto& a : ra)
    resolveAttachments.emplace_back(a.getReference());
  depthStencilAttachment = da.getReference();
}

// Be advised : resulting description is as long valid as SubpassDefinition exists.
// We are passing pointers to internal elements here
VkSubpassDescription SubpassDefinition::getDescription() const
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

SubpassDependencyDefinition::SubpassDependencyDefinition(uint32_t ss, uint32_t ds, VkPipelineStageFlags ssm, VkPipelineStageFlags dsm, VkAccessFlags sam, VkAccessFlags dam, VkDependencyFlags df)
  : srcSubpass{ ss }, dstSubpass{ ds }, srcStageMask{ ssm }, dstStageMask{ dsm }, srcAccessMask{ sam }, dstAccessMask{ dam }, dependencyFlags{df}
{
}

VkSubpassDependency SubpassDependencyDefinition::getDependency() const
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
  : RenderCommand(RenderCommand::commRenderPass)
{

}

//RenderPass::RenderPass(const std::vector<AttachmentDefinition>& a, const std::vector<SubpassDefinition>& s, const std::vector<SubpassDependencyDefinition>& d)
//  : RenderCommand(RenderCommand::commRenderPass), attachments(a), subpasses(s), dependencies(d)
//{
//}

RenderPass::~RenderPass()
{
  for (auto& pddit : perDeviceData)
    vkDestroyRenderPass(pddit.first, pddit.second.renderPass, nullptr);
}

void RenderPass::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ renderContext.vkDevice, PerDeviceData() }).first;
  if (!pddit->second.dirty)
    return;
  if (pddit->second.renderPass != VK_NULL_HANDLE)
    vkDestroyRenderPass(pddit->first, pddit->second.renderPass, nullptr);

  std::vector<VkAttachmentDescription> attachmentDescriptions;
  for (const auto& ad : attachments)
    attachmentDescriptions.emplace_back(ad.getDescription());

  std::vector<VkSubpassDescription> subpassDescriptions;
  for (const auto& sp : subpasses)
    subpassDescriptions.emplace_back(sp.getDescription());

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
  VK_CHECK_LOG_THROW( vkCreateRenderPass(renderContext.vkDevice, &renderPassCI, nullptr, &pddit->second.renderPass), "Could not create default render pass" );
  pddit->second.dirty = false;
}

VkRenderPass RenderPass::getHandle(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.renderPass;
}

void RenderPass::validateGPUData(ValidateGPUVisitor& validateVisitor)
{
  validateVisitor.renderContext.setRenderPass(this);
  validate(validateVisitor.renderContext);
  uint32_t subpassIndex = 0;
  for (auto operation : renderOperations)
  {
    validateVisitor.renderContext.setSubpassIndex(subpassIndex);
    validateVisitor.renderContext.setRenderOperation(operation.get());

    operation->sceneNode->accept(validateVisitor);

    subpassIndex++;
  }
  validateVisitor.renderContext.setRenderPass(NULL);
  validateVisitor.renderContext.setSubpassIndex(0);
  validateVisitor.renderContext.setRenderOperation(nullptr);
}

void RenderPass::buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor)
{
  commandVisitor.renderContext.setRenderPass(this);
  commandVisitor.commandBuffer->cmdBeginRenderPass
  (
    commandVisitor.renderContext.surface,
    commandVisitor.renderContext.renderPass,
    commandVisitor.renderContext.surface->renderWorkflow->frameBuffer.get(),
    commandVisitor.renderContext.activeIndex,
    makeVkRect2D(0, 0, commandVisitor.renderContext.surface->swapChainSize.width, commandVisitor.renderContext.surface->swapChainSize.height),
    clearValues,
    renderOperations[0]->subpassContents
  );

  for (uint32_t subpassIndex = 0; subpassIndex < renderOperations.size(); ++subpassIndex)
  {
    commandVisitor.renderContext.setSubpassIndex(subpassIndex);
    commandVisitor.renderContext.setRenderOperation(renderOperations[subpassIndex].get());
    if (renderOperations[subpassIndex]->subpassContents == VK_SUBPASS_CONTENTS_INLINE)
    {
      renderOperations[subpassIndex]->sceneNode->accept(commandVisitor);
    }
    else
    {
      // FIXME : execute secondary command buffer
    }
    if (subpassIndex < renderOperations.size()-1)
    {
      commandVisitor.commandBuffer->cmdNextSubPass(renderOperations[subpassIndex + 1]->subpassContents);
    }
  }
  commandVisitor.commandBuffer->cmdEndRenderPass();
  commandVisitor.renderContext.setRenderPass(NULL);
  commandVisitor.renderContext.setSubpassIndex(0);
  commandVisitor.renderContext.setRenderOperation(nullptr);
}

ComputePass::ComputePass()
  : RenderCommand(RenderCommand::commComputePass)
{
}

void ComputePass::validateGPUData(ValidateGPUVisitor& validateVisitor)
{
  validateVisitor.renderContext.setRenderOperation(computeOperation.get());
  computeOperation->sceneNode->accept(validateVisitor);
  validateVisitor.renderContext.setRenderOperation(nullptr);
}

void ComputePass::buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor)
{
  commandVisitor.renderContext.setRenderOperation(computeOperation.get());
  computeOperation->sceneNode->accept(commandVisitor);
  commandVisitor.renderContext.setRenderOperation(nullptr);
}

