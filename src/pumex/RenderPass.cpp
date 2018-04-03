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
#include <pumex/RenderWorkflow.h>

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

SubpassDefinition::SubpassDefinition()
  : pipelineBindPoint{ VK_PIPELINE_BIND_POINT_GRAPHICS }, depthStencilAttachment{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED }, flags{ 0 }
{
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

SubpassDefinition& SubpassDefinition::operator=(const SubpassDefinition& subpassDefinition)
{
  if (this != &subpassDefinition)
  {
    pipelineBindPoint      = subpassDefinition.pipelineBindPoint;
    inputAttachments       = subpassDefinition.inputAttachments;
    colorAttachments       = subpassDefinition.colorAttachments;
    resolveAttachments     = subpassDefinition.resolveAttachments;
    depthStencilAttachment = subpassDefinition.depthStencilAttachment;
    preserveAttachments    = subpassDefinition.preserveAttachments;
    flags                  = subpassDefinition.flags;
  }
  return *this;
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
{

}

RenderPass::~RenderPass()
{
  for (auto& pddit : perDeviceData)
    vkDestroyRenderPass(pddit.first, pddit.second.renderPass, nullptr);
}

void RenderPass::initializeAttachments(const std::vector<std::shared_ptr<WorkflowResource>>& resourceVector, const std::unordered_map<std::string, uint32_t>& attachmentIndex, std::vector<VkImageLayout>& lastLayout)
{
  attachments.clear();
  clearValues.clear();

  for (uint32_t i = 0; i < resourceVector.size(); ++i)
  {
    auto res = resourceVector[i];
    auto resType = res->resourceType;
    if (resType->metaType != RenderWorkflowResourceType::Attachment)
      continue;

    uint32_t attIndex = attachmentIndex.at(res->name);
    attachments.push_back( AttachmentDefinition(
      attIndex,
      resType->attachment.format,
      resType->attachment.samples,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE, //colorDepthAttachment ? (VkAttachmentLoadOp)firstLoadOp[attIndex].loadType : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      VK_ATTACHMENT_STORE_OP_DONT_CARE, //colorDepthAttachment && mustSaveResource ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE, // stencilAttachment ? (VkAttachmentLoadOp)firstLoadOp[attIndex].loadType : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      VK_ATTACHMENT_STORE_OP_DONT_CARE, //stencilAttachment && mustSaveResource ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
      lastLayout[attIndex],
      lastLayout[attIndex],
      0
    ));

  }
  clearValues.resize(attachments.size());
  std::fill(clearValues.begin(), clearValues.end(), makeColorClearValue(glm::vec4(0.0f)));
}

void RenderPass::addSubPass(std::shared_ptr<RenderSubPass> renderSubPass)
{
  renderSubPass->renderPass = shared_from_this();
  renderSubPass->subpassIndex = subPasses.size();
  subPasses.push_back(renderSubPass);
}

void RenderPass::updateAttachments(std::shared_ptr<RenderSubPass> renderSubPass, const std::unordered_map<std::string, uint32_t>& attachmentIndex, std::vector<VkImageLayout>& lastLayout, std::vector<FrameBufferImageDefinition>& frameBufferDefinitions)
{
  // fill attachment information with render subpass specifics ( initial layout, final layout, load op, clear values )
  std::shared_ptr<RenderWorkflow> rw = renderSubPass->operation->renderWorkflow.lock();
  auto allAttachmentTransitions = rw->getOperationIO(renderSubPass->operation->name, rttAllAttachments);
  for (auto transition : allAttachmentTransitions)
  {
    uint32_t attIndex = attachmentIndex.at(transition->resource->name);

    frameBufferDefinitions[attIndex].usage |= getAttachmentUsage(transition->attachment.layout);
    attachments[attIndex].finalLayout      = lastLayout[attIndex] = transition->attachment.layout;

    AttachmentType at = transition->resource->resourceType->attachment.attachmentType;
    bool colorDepthAttachment   = (at == atSurface) || (at == atColor) || (at == atDepth) || (at == atDepthStencil);
    bool stencilAttachment      = (at == atDepthStencil) || (at == atStencil);
    bool stencilDepthAttachment = (at == atDepth) || (at == atDepthStencil) || (at == atStencil);

    // if it's an output transition
    if ((transition->transitionType & rttAllOutputs) != 0)
    {
      if (attachments[attIndex].initialLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        attachments[attIndex].initialLayout = transition->attachment.layout;
    }

    // if it's an input transition
    if ((transition->transitionType & rttAllInputs) != 0)
    {
      // FIXME
    }

    attachments[attIndex].loadOp        = colorDepthAttachment ? (VkAttachmentLoadOp)transition->attachment.load.loadType : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[attIndex].stencilLoadOp = stencilAttachment    ? (VkAttachmentLoadOp)transition->attachment.load.loadType : VK_ATTACHMENT_LOAD_OP_DONT_CARE;

    if (stencilDepthAttachment)
      clearValues[attIndex] = makeDepthStencilClearValue(transition->attachment.load.clearColor.x, transition->attachment.load.clearColor.y);
    else
      clearValues[attIndex] = makeColorClearValue(transition->attachment.load.clearColor);
  }
}

void RenderPass::finalizeAttachments()
{
  // FIXME - collect information about resources that must be saved and/or preserved

  // Resource must be saved when it was tagged as persistent, ot it is a swapchain surface  or it will be used later
  // FIXME : resource will be used later if any of its subsequent uses has loadOpLoad or it is used as input - THESE CONDITIONS ARE NOT EXAMINED AT THE MOMENT
  //bool mustSaveResource = resType->persistent ||
  //  resType->attachment.attachmentType == atSurface ||
  //  resourcesUsedAfterRenderPass.find(res->name) != resourcesUsedAfterRenderPass.end();

  //    VK_ATTACHMENT_STORE_OP_DONT_CARE, //colorDepthAttachment && mustSaveResource ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
  //    VK_ATTACHMENT_STORE_OP_DONT_CARE, //stencilAttachment && mustSaveResource ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,

  // FIXME - preserve attachments
}

void RenderPass::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ renderContext.vkDevice, PerDeviceData() }).first;
  if (pddit->second.valid)
    return;
  if (pddit->second.renderPass != VK_NULL_HANDLE)
    vkDestroyRenderPass(pddit->first, pddit->second.renderPass, nullptr);

  std::vector<VkAttachmentDescription> attachmentDescriptions;
  for (const auto& ad : attachments)
    attachmentDescriptions.emplace_back(ad.getDescription());

  std::vector<VkSubpassDescription> subpassDescriptions;
  for (auto& sp : subPasses)
    subpassDescriptions.emplace_back(sp.lock()->definition.getDescription());

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
  pddit->second.valid = true;
}

VkRenderPass RenderPass::getHandle(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.renderPass;
}
ResourceBarrier::ResourceBarrier(std::shared_ptr<Resource> r, VkAccessFlags sam, VkAccessFlags dam, uint32_t sqfi, uint32_t dqfi, VkImageLayout ol, VkImageLayout nl)
  : resource{ r }, srcAccessMask{ sam }, dstAccessMask{ dam }, srcQueueFamilyIndex{ sqfi }, dstQueueFamilyIndex{ dqfi }, oldLayout{ ol }, newLayout{ nl }
{
}

ResourceBarrierGroup::ResourceBarrierGroup(VkPipelineStageFlags ssm, VkPipelineStageFlags dsm, VkDependencyFlags d)
  : srcStageMask{ ssm }, dstStageMask{ dsm }, dependencyFlags{ d }
{
}

RenderCommand::RenderCommand(RenderCommand::CommandType ct)
  : commandType{ ct }
{
}

RenderSubPass::RenderSubPass()
  : RenderCommand(RenderCommand::ComputePass)
{

}

void RenderSubPass::buildSubPassDefinition(const std::unordered_map<std::string, uint32_t>& attachmentIndex)
{
  // Fun fact : VkSubpassDescription with compute bind point is forbidden by Vulkan spec
  VkPipelineBindPoint              bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  std::vector<AttachmentReference> ia;
  std::vector<AttachmentReference> oa;
  std::vector<AttachmentReference> ra;
  AttachmentReference              dsa;
  std::vector<uint32_t>            pa;


  std::shared_ptr<RenderWorkflow> rw = operation->renderWorkflow.lock();
  auto inputAttachments   = rw->getOperationIO(operation->name, rttAttachmentInput);
  auto outputAttachments  = rw->getOperationIO(operation->name, rttAttachmentOutput);
  auto resolveAttachments = rw->getOperationIO(operation->name, rttAttachmentResolveOutput);
  auto depthAttachments   = rw->getOperationIO(operation->name, rttAttachmentDepthOutput);

  for (auto inputAttachment : inputAttachments)
    ia.push_back({ attachmentIndex.at(inputAttachment->resource->name), inputAttachment->attachment.layout });
  for (auto outputAttachment : outputAttachments)
  {
    oa.push_back({ attachmentIndex.at(outputAttachment->resource->name), outputAttachment->attachment.layout });

    if (!resolveAttachments.empty())
    {
      auto it = std::find_if(resolveAttachments.begin(), resolveAttachments.end(), [outputAttachment](const std::shared_ptr<ResourceTransition>& rt) -> bool { return rt->attachment.resolveResource == outputAttachment->resource; });
      if (it != resolveAttachments.end())
        ra.push_back({ attachmentIndex.at((*it)->resource->name), (*it)->attachment.layout });
      else
        ra.push_back({ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
    }
    else
      ra.push_back({ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
  }
  if (!depthAttachments.empty())
    dsa = { attachmentIndex.at(depthAttachments[0]->resource->name), depthAttachments[0]->attachment.layout };
  else
    dsa = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };

  definition = SubpassDefinition(VK_PIPELINE_BIND_POINT_GRAPHICS, ia, oa, ra, dsa, pa);
}

void RenderSubPass::validateGPUData(ValidateGPUVisitor& validateVisitor)
{
  validateVisitor.renderContext.setRenderPass(renderPass);
  renderPass->validate(validateVisitor.renderContext);

  validateVisitor.renderContext.setSubpassIndex(subpassIndex);
  validateVisitor.renderContext.setRenderOperation(operation);

  operation->sceneNode->accept(validateVisitor);

  validateVisitor.renderContext.setRenderOperation(nullptr);
  validateVisitor.renderContext.setSubpassIndex(0);
  validateVisitor.renderContext.setRenderPass(nullptr);
}

void RenderSubPass::buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor)
{
  commandVisitor.renderContext.setRenderPass(renderPass);
  commandVisitor.renderContext.setSubpassIndex(subpassIndex);
  commandVisitor.renderContext.setRenderOperation(operation);
  for (auto barrierGroup : barriersBeforeOp)
    commandVisitor.commandBuffer->cmdPipelineBarrier(commandVisitor.renderContext, barrierGroup.first, barrierGroup.second);

  if (subpassIndex == 0)
  {
    commandVisitor.commandBuffer->cmdBeginRenderPass
    (
      commandVisitor.renderContext.surface,
      this,
      commandVisitor.renderContext.surface->renderWorkflow->frameBuffer.get(),
      commandVisitor.renderContext.activeIndex,
      makeVkRect2D(0, 0, commandVisitor.renderContext.surface->swapChainSize.width, commandVisitor.renderContext.surface->swapChainSize.height),
      renderPass->clearValues,
      operation->subpassContents
    );
  }
  else
  {
    commandVisitor.commandBuffer->cmdNextSubPass(this, operation->subpassContents);
  }

  // FIXME - this should be moved somewhere else
  commandVisitor.commandBuffer->cmdSetViewport(0, { makeViewport(0, 0, commandVisitor.renderContext.surface->swapChainSize.width, commandVisitor.renderContext.surface->swapChainSize.height, 0.0f, 1.0f) });
  commandVisitor.commandBuffer->cmdSetScissor(0, { makeVkRect2D(0, 0, commandVisitor.renderContext.surface->swapChainSize.width, commandVisitor.renderContext.surface->swapChainSize.height) });

  operation->sceneNode->accept(commandVisitor);


  if (renderPass->subPasses.size() == subpassIndex + 1)
  {
    commandVisitor.commandBuffer->cmdEndRenderPass();
  }

  for (auto barrierGroup : barriersAfterOp)
    commandVisitor.commandBuffer->cmdPipelineBarrier(commandVisitor.renderContext, barrierGroup.first, barrierGroup.second);

  commandVisitor.renderContext.setRenderOperation(nullptr);
  commandVisitor.renderContext.setSubpassIndex(0);
  commandVisitor.renderContext.setRenderPass(nullptr);
}

ComputePass::ComputePass()
  : RenderCommand(RenderCommand::ComputePass)
{
}

void ComputePass::validateGPUData(ValidateGPUVisitor& validateVisitor)
{
  validateVisitor.renderContext.setRenderOperation(operation);
  operation->sceneNode->accept(validateVisitor);
  validateVisitor.renderContext.setRenderOperation(nullptr);
}

void ComputePass::buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor)
{
  commandVisitor.renderContext.setRenderOperation(operation);

  for (auto barrierGroup : barriersBeforeOp)
    commandVisitor.commandBuffer->cmdPipelineBarrier(commandVisitor.renderContext, barrierGroup.first, barrierGroup.second);

  operation->sceneNode->accept(commandVisitor);

  for (auto barrierGroup : barriersAfterOp)
    commandVisitor.commandBuffer->cmdPipelineBarrier(commandVisitor.renderContext, barrierGroup.first, barrierGroup.second);

  commandVisitor.renderContext.setRenderOperation(nullptr);
}

