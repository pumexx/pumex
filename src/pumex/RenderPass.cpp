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
#include <pumex/Device.h>
#include <pumex/utils/Log.h>

using namespace pumex;

RenderOperationAttachment::RenderOperationAttachment()
  : name{ }, operationLayout{ VK_IMAGE_LAYOUT_UNDEFINED }, loadOperation{ loadOpDontCare() }
{

}


RenderOperationAttachment::RenderOperationAttachment(const std::string& n, VkImageLayout l)
  : name{ n }, operationLayout{ l }, loadOperation{ loadOpDontCare() }
{
}

RenderOperationAttachment::RenderOperationAttachment(const std::string& n, VkImageLayout l, LoadOp op)
  : name{ n }, operationLayout{ l }, loadOperation{ op }
{
}

RenderOperation::RenderOperation()
  : name{ "undefined" }, operationType{ RenderOperation::Graphics }
{

}


RenderOperation::RenderOperation(const std::string& n, RenderOperation::Type t)
  : name{ n }, operationType{ t }
{
}

void RenderOperation::addAttachmentInput(const RenderOperationAttachment& opAttachment)
{
  inputAttachments[opAttachment.name] = opAttachment;
}
void RenderOperation::addAttachmentOutput(const RenderOperationAttachment& opAttachment)
{
  outputAttachments[opAttachment.name] = opAttachment;
}

void RenderOperation::addResolveOutput(const RenderOperationAttachment& opAttachment)
{
  resolveAttachments[opAttachment.name] = opAttachment;
}

void RenderOperation::setDepthOutput(const RenderOperationAttachment& opAttachment)
{
  depthAttachment = opAttachment;
}

RenderWorkflowAttachment::RenderWorkflowAttachment(const std::string& n, AttachmentType at, VkFormat f, VkSampleCountFlagBits s, AttachmentSize st, glm::vec2 is, bool p)
  : name { n }, attachmentType{ at }, format{ f }, samples{ s }, sizeType{ st }, imageSize{ is }, persistent{ p }
{
}

RenderWorkflow::RenderWorkflow(const std::string& n, const std::vector<RenderWorkflowAttachment>& atts)
  : name{ n }
{
  for(auto& s : atts)
    attachments[s.name] = s;
}

RenderWorkflow::~RenderWorkflow()
{

}

RenderOperation& RenderWorkflow::addRenderOperation(const RenderOperation& op)
{
  renderOperations[op.name] = op;
  return renderOperations[op.name];
}

RenderOperation& RenderWorkflow::getOperation(const std::string& opName)
{
  auto it = renderOperations.find(opName);
  CHECK_LOG_THROW(it == renderOperations.end(), "RenderWorkflow : there is no operation with name " + opName);
  return it->second;
}

const RenderOperation& RenderWorkflow::getOperation(const std::string& opName) const
{
  auto it = renderOperations.find(opName);
  CHECK_LOG_THROW(it == renderOperations.end(), "RenderWorkflow : there is no operation with name " + opName);
  return it->second;
}

void topologicalSort(std::vector<std::string>& results, const std::vector<std::string>& nodesToSort, const std::unordered_multimap<std::string, std::string>& nodeInputs, const std::unordered_multimap<std::string, std::string>& nodeOutputs)
{
  results.clear();
  // make a copy of parameters
  std::set<std::string> nodes;
  for (const auto& n : nodesToSort)
    nodes.insert(n);
  auto inputs     = nodeInputs;
  auto outputs    = nodeOutputs;
  while (nodes.size() > 0)
  {
    uint32_t sortedNodes = 0;
    for (auto nit = nodes.begin(); nit!=nodes.end(); )
    {
      // add to results all nodes that have no inputs
      auto inputPair = inputs.equal_range(*nit);
      if (inputPair.first != inputPair.second)
      {
        ++nit;
        continue;
      }
      results.push_back(*nit);
      ++sortedNodes;
      // remove outputs of nit, but copy it to temporary storage for now
      auto outputPair = outputs.equal_range(*nit);
      std::set<std::string> inputsToDelete;
      for (auto it = outputPair.first; it != outputPair.second; ++it)
        inputsToDelete.insert(it->second);
      outputs.erase(outputPair.first, outputPair.second);
      // ok, now remove all inputs, where name is equal to one of outputs of nit
      for (auto it = inputs.begin(); it != inputs.end(); )
      {
        if (inputsToDelete.find(it->second) != inputsToDelete.end())
          it = inputs.erase(it);
        else
          ++it;
      }
      nit = nodes.erase(nit);
    }
    CHECK_LOG_THROW(sortedNodes == 0, "topologicalSort : check your workflow for cycles, missing inputs/outputs...");
  }
}

void groupOperations(std::vector<std::vector<std::string>>& groupedOperations, const std::vector<std::string>& sortedOperations, const std::unordered_multimap<std::string, std::string>& operationInputs, const std::unordered_multimap<std::string, std::string>& operationOutputs)
{
  groupedOperations.clear();

}


void RenderWorkflow::compile()
{
  std::vector<std::string> operations, sortedOperations;
  std::unordered_multimap<std::string, std::string> operationInputs, operationOutputs;
  // fill table with input and output names
  for (auto opit : renderOperations)
  {
    operations.push_back(opit.first);
    for (auto it : opit.second.inputAttachments)
      operationInputs.insert( { opit.first, it.first } );
    for (auto out : opit.second.outputAttachments)
      operationOutputs.insert( { opit.first, out.first } );
    for (auto out : opit.second.resolveAttachments)
      operationOutputs.insert( { opit.first, out.first } );
    if(!opit.second.depthAttachment.name.empty())
      operationOutputs.insert( { opit.first, opit.second.depthAttachment.name } );
  }
  // sort topologically, so that creation of inputs predates using them as outputs
  topologicalSort(sortedOperations, operations, operationInputs, operationOutputs);
  // group operations into render passes
  std::vector<std::vector<std::string>> groupedOperations;
  groupOperations(groupedOperations, sortedOperations, operationInputs, operationOutputs);



}




/***********************/

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

RenderPass::RenderPass(const std::vector<AttachmentDefinition>& a, const std::vector<SubpassDefinition>& s, const std::vector<SubpassDependencyDefinition>& d)
  : attachments(a), subpasses(s), dependencies(d)
{
}

RenderPass::~RenderPass()
{
  for (auto& pddit : perDeviceData)
    vkDestroyRenderPass(pddit.first, pddit.second.renderPass, nullptr);
}

void RenderPass::validate(Device* device)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
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
  VK_CHECK_LOG_THROW( vkCreateRenderPass(device->device, &renderPassCI, nullptr, &pddit->second.renderPass), "Could not create default render pass" );
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
