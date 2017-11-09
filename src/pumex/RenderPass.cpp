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
#include <sstream>
#include <iterator>

using namespace pumex;

RenderWorkflowResourceType::RenderWorkflowResourceType()
  : metaType{ Undefined }, typeName{}, format{ VK_FORMAT_UNDEFINED }, samples{ VK_SAMPLE_COUNT_1_BIT }, persistent{ false }
{

}

RenderWorkflowResourceType::RenderWorkflowResourceType(const std::string& tn, VkFormat f, VkSampleCountFlagBits s, bool p, AttachmentType at, AttachmentSize st, glm::vec2 is)
  : metaType{ Attachment }, typeName{ tn }, format{ f }, samples{ s }, persistent{ p }, attachment{ at, st, is }
{
}

WorkflowResource::WorkflowResource()
  : name{ }, operationLayout{ VK_IMAGE_LAYOUT_UNDEFINED }, loadOperation{ loadOpDontCare() }
{

}

WorkflowResource::WorkflowResource(const std::string& n, const std::string& tn, VkImageLayout l)
  : name{ n }, typeName{ tn }, operationLayout { l }, loadOperation{ loadOpDontCare() }
{
}

WorkflowResource::WorkflowResource(const std::string& n, const std::string& tn, VkImageLayout l, LoadOp op)
  : name{ n }, typeName{ tn }, operationLayout{ l }, loadOperation{ op }
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

void RenderOperation::addAttachmentInput(const WorkflowResource& opAttachment)
{
  inputAttachments[opAttachment.name] = opAttachment;
}
void RenderOperation::addAttachmentOutput(const WorkflowResource& opAttachment)
{
  outputAttachments[opAttachment.name] = opAttachment;
}

void RenderOperation::addAttachmentResolveOutput(const WorkflowResource& opAttachment)
{
  resolveAttachments[opAttachment.name] = opAttachment;
}

void RenderOperation::setAttachmentDepthOutput(const WorkflowResource& opAttachment)
{
  depthAttachment = opAttachment;
}

std::vector<const WorkflowResource*> RenderOperation::getInputsOutputs(IOType ioTypes) const
{
  std::vector<const WorkflowResource*> results;
  if (ioTypes & AttachmentInput)
  {
    for (auto it = inputAttachments.begin(); it != inputAttachments.end(); ++it)
      results.push_back(&(it->second));
  }
  if (ioTypes & AttachmentOutput)
  {
    for (auto it = outputAttachments.begin(); it != outputAttachments.end(); ++it)
      results.push_back(&(it->second));
  }
  if (ioTypes & AttachmentResolveOutput)
  {
    for (auto it = resolveAttachments.begin(); it != resolveAttachments.end(); ++it)
      results.push_back(&(it->second));
  }
  if (ioTypes & AttachmentDepthOutput)
  {
    if( !depthAttachment.name.empty())
      results.push_back(&depthAttachment);
  }
  return results;
}

RenderWorkflow::RenderWorkflow(const std::string& n)
  : name{ n }
{
}

RenderWorkflow::~RenderWorkflow()
{

}

void RenderWorkflow::addResourceType(const RenderWorkflowResourceType& tp)
{
  resourceTypes[tp.typeName] = tp;
}


RenderOperation& RenderWorkflow::addRenderOperation(const RenderOperation& op)
{
  renderOperations[op.name] = op;
  return renderOperations[op.name];
}

const RenderWorkflowResourceType& RenderWorkflow::getResourceType(const std::string& typeName) const
{
  auto it = resourceTypes.find(typeName);
  CHECK_LOG_THROW(it == resourceTypes.end(), "RenderWorkflow : there is no resource type with name " + typeName);
  return it->second;
}

const RenderOperation& RenderWorkflow::getOperation(const std::string& opName) const
{
  auto it = renderOperations.find(opName);
  CHECK_LOG_THROW(it == renderOperations.end(), "RenderWorkflow : there is no operation with name " + opName);
  return it->second;
}

void RenderWorkflow::getAttachmentSizes(const std::vector<const WorkflowResource*>& resources, std::vector<AttachmentSize>& attachmentSizes, std::vector<glm::vec2>& imageSizes) const
{
  for (auto rit = resources.begin(); rit != resources.end(); ++rit)
  {
    auto& rType = getResourceType((*rit)->typeName);
    if (rType.metaType != RenderWorkflowResourceType::Attachment)
      continue;
    attachmentSizes.push_back(rType.attachment.sizeType);
    imageSizes.push_back(rType.attachment.imageSize);
  }
}

std::vector<const RenderOperation*> RenderWorkflow::findOperations(RenderOperation::IOType ioTypes, const std::vector<const WorkflowResource*>& ioObjects) const
{
  std::vector<const RenderOperation*> results;
  for (auto it = renderOperations.begin(); it != renderOperations.end(); ++it)
  {
    auto ioObjectsX = it->second.getInputsOutputs(ioTypes);
    bool found = false;
    for (auto iit = ioObjects.begin(); iit != ioObjects.end(); ++iit)
    {
      for (auto xit = ioObjectsX.begin(); xit != ioObjectsX.end(); ++xit)
      {
        if ((*iit)->name == (*xit)->name)
        {
          found = true;
          break;
        }
      }
      if (found)
        break;
    }
    if (found)
      results.push_back(&(it->second));
  }
  return results;
}

std::vector<std::string> RenderWorkflow::findFinalOperations() const
{
  std::vector<std::string> finalOperations;
  for (auto oit = renderOperations.begin(); oit != renderOperations.end(); ++oit )
  {
    auto outputs = oit->second.getInputsOutputs(RenderOperation::AllOutputs);
    bool connected = false;
    for (auto iit = renderOperations.begin(); iit != renderOperations.end(); ++iit)
    {
      auto inputs = iit->second.getInputsOutputs(RenderOperation::AllInputs);
      // check if operation oit has some outputs that are connected to inputs of operation iit
      for (auto ot : outputs)
      {
        for (auto it : inputs)
        {
          if (ot->name == it->name)
          {
            connected = true;
            break;
          }
        }
        if (connected)
          break;
      }
      if (connected)
        break;
    }
    if (!connected)
      finalOperations.push_back(oit->first);
  }
  return finalOperations;
}



void RenderWorkflow::compile(RenderWorkflowCompiler* compiler)
{
  compiler->compile(*this);
};

void StandardRenderWorkflowCompiler::compile(RenderWorkflow& workflow)
{
  // verify operations
  verifyOperations(workflow);

  // build a vector storing proper sequence of operations
  std::vector<std::string> operations = scheduleOperations(workflow);

}

void StandardRenderWorkflowCompiler::verifyOperations(RenderWorkflow& workflow)
{
  std::ostringstream os;
  // check if all attachments have the same size in each operation
  for (auto it = workflow.renderOperations.cbegin(); it != workflow.renderOperations.end(); ++it)
  {
    auto opResources = it->second.getInputsOutputs(RenderOperation::AllAttachments);
    std::vector<AttachmentSize> attachmentSizes;
    std::vector<glm::vec2>      imageSizes;
    workflow.getAttachmentSizes(opResources, attachmentSizes, imageSizes);
    bool sameSize = true;
    for (uint32_t i = 0; i < attachmentSizes.size() - 1; ++i)
    {
      if (attachmentSizes[i] != attachmentSizes[i + 1] || imageSizes[i] != imageSizes[i + 1])
      {
        sameSize = false;
        break;
      }
    }
    if (!sameSize)
    {
      os << "Error: Operation <" << it->first << "> : not all attachments have the same size" << std::endl;
    }
  }

  // if there are some errors - throw exception
  std::string results;
  results = os.str();
  CHECK_LOG_THROW(!results.empty(), "Errors in workflow operations :\n" + results);
}

struct RenderWorkflowCostCalculator
{
  virtual float calculateWorkflowCost(const RenderWorkflow& workflow, const std::vector<std::string>& operationSchedule) const = 0;
};

// note the lack of references in parameters - workflow is copied here
std::vector<std::string> recursiveScheduleOperations(RenderWorkflow workflow, const std::string& op, RenderWorkflowCostCalculator* costCalculator)
{
  if(!op.empty())
    workflow.renderOperations.erase(op);
  std::vector<std::string> finalOperations = workflow.findFinalOperations();
  std::vector<std::vector<std::string>> results;
  std::vector<float> cost;
  for (const auto& x : finalOperations)
  {
    auto xx = recursiveScheduleOperations(workflow, x, costCalculator);
    if(!op.empty())
      xx.push_back(op);
    cost.push_back(costCalculator->calculateWorkflowCost(workflow, xx));
    results.push_back(xx);
  }
  if (results.empty())
  {
    std::vector<std::string> res;
    if (!op.empty())
      res.push_back(op);
    return res;
  }
  // find a result with lowest cost
  auto minit = std::min_element(cost.begin(), cost.end());
  auto i = std::distance(cost.begin(), minit);
  // return it
  return results[i];
}


struct StandardRenderWorkflowCostCalculator : RenderWorkflowCostCalculator
{
  StandardRenderWorkflowCostCalculator(const RenderWorkflow& workflow)
  {
    attachmentTag = tagOperationByAttachmentType(workflow);
  }
  std::unordered_map<std::string, int> tagOperationByAttachmentType(const RenderWorkflow& workflow)
  {
    std::unordered_map<int, std::tuple<AttachmentSize, glm::vec2>> tags;
    std::unordered_map<std::string, int> attachmentTag;
    int currentTag = 0;
    for (auto opit : workflow.renderOperations)
    {
      if (opit.second.operationType != RenderOperation::Graphics)
      {
        attachmentTag.insert({ opit.first, currentTag++ });
        continue;
      }
      auto opAttachments = opit.second.getInputsOutputs(RenderOperation::AllAttachments);
      std::vector<AttachmentSize> attachmentSizes;
      std::vector<glm::vec2>      imageSizes;
      workflow.getAttachmentSizes(opAttachments, attachmentSizes, imageSizes);
      // operations have the same sizes - just take the first one
      AttachmentSize atSize = attachmentSizes.empty() ? AttachmentSize::asUndefined : attachmentSizes[0];
      glm::vec2      imSize = imageSizes.empty() ? glm::vec2(0.0) : imageSizes[0];
      int tagFound = -1;
      for (auto tit : tags)
      {
        if (std::get<0>(tit.second) == atSize && std::get<1>(tit.second) == imSize)
        {
          tagFound = tit.first;
          break;
        }
      }
      if (tagFound < 0)
      {
        tagFound = currentTag++;
        tags.insert({ tagFound,std::make_tuple(atSize,imSize) });
      }
      attachmentTag.insert({ opit.first, tagFound });
    }
    return attachmentTag;
  }

  float calculateWorkflowCost(const RenderWorkflow& workflow, const std::vector<std::string>& operationSchedule) const override
  {
    if (operationSchedule.empty())
      return 0.0f;
    float result = 0.0f;
    int tag = attachmentTag.at(operationSchedule[0]);
    for (int i = 1; i < operationSchedule.size(); ++i)
    {
      int newTag = attachmentTag.at(operationSchedule[i]);
      if (newTag != tag)
        result += 10.0f;
      tag = newTag;
    }
    return result;
  }

  std::unordered_map<std::string, int> attachmentTag;
};


std::vector<std::string> StandardRenderWorkflowCompiler::scheduleOperations(RenderWorkflow& workflow)
{
  // build an operator for cost calculations
  StandardRenderWorkflowCostCalculator costCalculator(workflow);

  return recursiveScheduleOperations(workflow, "", &costCalculator);
}

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
