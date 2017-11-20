//
// Copyright(c) 2017 Paweł Księżopolski ( pumexx )
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

#include <pumex/RenderWorkflow.h>
#include <algorithm>
#include <sstream>
#include <iterator>
#include <pumex/Device.h>
#include <pumex/utils/Log.h>
#include <pumex/FrameBuffer.h>
#include <pumex/RenderPass.h>

using namespace pumex;

RenderWorkflowResourceType::RenderWorkflowResourceType()
  : metaType{ Undefined }, typeName{}, format{ VK_FORMAT_UNDEFINED }, samples{ VK_SAMPLE_COUNT_1_BIT }, persistent{ false }
{

}

RenderWorkflowResourceType::RenderWorkflowResourceType(const std::string& tn, VkFormat f, VkSampleCountFlagBits s, bool p, AttachmentType at, const AttachmentSize& as)
  : metaType{ Attachment }, typeName{ tn }, format{ f }, samples{ s }, persistent{ p }, attachment{ at, as }
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

Node::Node()
{
}

Node::~Node()
{
  parents.clear();
}

ComputeNode::ComputeNode()
  : Node()
{
}

ComputeNode::~ComputeNode()
{
}

NodeGroup::NodeGroup()
{
}

NodeGroup::~NodeGroup()
{
  children.clear();
}

void NodeGroup::addChild(std::shared_ptr<Node> child)
{
}

RenderOperation::RenderOperation(const std::string& n, RenderOperation::Type t, VkSubpassContents sc)
  : name{ n }, operationType{ t }, subpassContents{ sc }
{
}

RenderOperation::~RenderOperation()
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

GraphicsOperation::GraphicsOperation(const std::string& n, VkSubpassContents sc)
  : RenderOperation(n, RenderOperation::Graphics, sc)
{
}

void GraphicsOperation::setNode(std::shared_ptr<Node> node)
{
  renderNode = node;
}

ComputeOperation::ComputeOperation(const std::string& n, VkSubpassContents sc)
  : RenderOperation(n, RenderOperation::Compute, sc)
{
}

void ComputeOperation::setNode(std::shared_ptr<ComputeNode> node)
{
  computeNode = node;
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

SubpassDefinition RenderOperation::buildSubPassDefinition(const std::unordered_map<std::string, uint32_t>& activeResourceIndex) const
{
  // Fun fact : VkSubpassDescription with compute bind point is forbidden by Vulkan spec
  VkPipelineBindPoint              bindPoint = (operationType == Graphics) ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;
  std::vector<AttachmentReference> ia;
  std::vector<AttachmentReference> oa;
  std::vector<AttachmentReference> ra;
  AttachmentReference              dsa;
  std::vector<uint32_t>            pa;

  for (auto it = inputAttachments.begin(); it != inputAttachments.end(); ++it)
    ia.push_back({ activeResourceIndex.at(it->first), it->second.operationLayout });
  for (auto it = outputAttachments.begin(); it != outputAttachments.end(); ++it)
    oa.push_back({ activeResourceIndex.at(it->first), it->second.operationLayout });
  for (auto it = resolveAttachments.begin(); it != resolveAttachments.end(); ++it)
    ra.push_back({ activeResourceIndex.at(it->first), it->second.operationLayout });
  if (!depthAttachment.name.empty())
    dsa = { activeResourceIndex.at(depthAttachment.name), depthAttachment.operationLayout };
  return SubpassDefinition(bindPoint, ia, oa, ra, dsa, pa);
}


RenderCommand::RenderCommand(RenderCommand::CommandType ct)
  : commandType{ ct }
{
}

RenderWorkflow::RenderWorkflow(const std::string& n, std::shared_ptr<pumex::RenderWorkflowCompiler> c)
  : name{ n }, compiler{ c }
{
}

RenderWorkflow::~RenderWorkflow()
{

}

void RenderWorkflow::addResourceType(const RenderWorkflowResourceType& tp)
{
  resourceTypes[tp.typeName] = tp;
}

void RenderWorkflow::addRenderOperation(std::shared_ptr<RenderOperation> op)
{
  renderOperations[op->name] = op;
}

void RenderWorkflow::addQueue(const QueueTraits& qt)
{
  queueTraits.push_back(qt);
}


const RenderWorkflowResourceType& RenderWorkflow::getResourceType(const std::string& typeName) const
{
  auto it = resourceTypes.find(typeName);
  CHECK_LOG_THROW(it == resourceTypes.end(), "RenderWorkflow : there is no resource type with name " + typeName);
  return it->second;
}

std::shared_ptr<RenderOperation> RenderWorkflow::getOperation(const std::string& opName) const
{
  auto it = renderOperations.find(opName);
  CHECK_LOG_THROW(it == renderOperations.end(), "RenderWorkflow : there is no operation with name " + opName);
  return it->second;
}


void RenderWorkflow::getAttachmentSizes(const std::vector<const WorkflowResource*>& resources, std::vector<AttachmentSize>& attachmentSizes) const
{
  for (auto rit = resources.begin(); rit != resources.end(); ++rit)
  {
    auto& rType = getResourceType((*rit)->typeName);
    if (rType.metaType != RenderWorkflowResourceType::Attachment)
      continue;
    attachmentSizes.push_back(rType.attachment.attachmentSize);
  }
}

std::vector<std::shared_ptr<RenderOperation>> RenderWorkflow::findOperations(RenderOperation::IOType ioTypes, const std::vector<const WorkflowResource*>& ioObjects) const
{
  std::vector<std::shared_ptr<RenderOperation>> results;
  for (auto it = renderOperations.begin(); it != renderOperations.end(); ++it)
  {
    auto ioObjectsX = it->second->getInputsOutputs(ioTypes);
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
      results.push_back(it->second);
  }
  return results;
}

std::vector<std::shared_ptr<RenderOperation>> RenderWorkflow::findFinalOperations() const
{
  std::vector<std::shared_ptr<RenderOperation>> finalOperations;
  for (auto oit = renderOperations.begin(); oit != renderOperations.end(); ++oit )
  {
    auto outputs = oit->second->getInputsOutputs(RenderOperation::AllOutputs);
    bool connected = false;
    for (auto iit = renderOperations.begin(); iit != renderOperations.end(); ++iit)
    {
      auto inputs = iit->second->getInputsOutputs(RenderOperation::AllInputs);
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
      finalOperations.push_back(oit->second);
  }
  return finalOperations;
}

void RenderWorkflow::compile()
{
  compiler->compile(*this);
};

void StandardRenderWorkflowCostCalculator::tagOperationByAttachmentType(const RenderWorkflow& workflow)
{
  std::unordered_map<int, AttachmentSize> tags;
  attachmentTag.clear();
  int currentTag = 0;
  for (auto opit : workflow.renderOperations)
  {
    if (opit.second->operationType != RenderOperation::Graphics)
    {
      attachmentTag.insert({ opit.first, currentTag++ });
      continue;
    }
    auto opAttachments = opit.second->getInputsOutputs(RenderOperation::AllAttachments);
    std::vector<AttachmentSize> attachmentSizes;
    workflow.getAttachmentSizes(opAttachments, attachmentSizes);
    // operations have the same sizes - just take the first one
    AttachmentSize atSize = attachmentSizes.empty() ? AttachmentSize() : attachmentSizes[0];
    int tagFound = -1;
    for (auto tit : tags)
    {
      if (tit.second == atSize)
      {
        tagFound = tit.first;
        break;
      }
    }
    if (tagFound < 0)
    {
      tagFound = currentTag++;
      tags.insert({ tagFound, atSize });
    }
    attachmentTag.insert({ opit.first, tagFound });
  }
}

float StandardRenderWorkflowCostCalculator::calculateWorkflowCost(const RenderWorkflow& workflow, const std::vector<std::shared_ptr<RenderOperation>>& operationSchedule) const
{
  if (operationSchedule.empty())
    return 0.0f;
  float result = 0.0f;
  int tag = attachmentTag.at(operationSchedule[0]->name);
  for (int i = 1; i < operationSchedule.size(); ++i)
  {
    int newTag = attachmentTag.at(operationSchedule[i]->name);
    if (newTag != tag)
      result += 10.0f;
    tag = newTag;
  }
  return result;
}

// note the lack of references in parameters - workflow is copied here
std::vector<std::shared_ptr<RenderOperation>> recursiveScheduleOperations(RenderWorkflow workflow, std::shared_ptr<RenderOperation> op, StandardRenderWorkflowCostCalculator* costCalculator)
{
  if (op.get() != nullptr)
    workflow.renderOperations.erase(op->name);
  std::vector<std::shared_ptr<RenderOperation>> finalOperations = workflow.findFinalOperations();
  std::vector<std::vector<std::shared_ptr<RenderOperation>>> results;
  std::vector<float> cost;
  for (const auto& x : finalOperations)
  {
    auto xx = recursiveScheduleOperations(workflow, x, costCalculator);
    if (op.get() != nullptr)
      xx.push_back(op);
    cost.push_back(costCalculator->calculateWorkflowCost(workflow, xx));
    results.push_back(xx);
  }
  if (results.empty())
  {
    std::vector<std::shared_ptr<RenderOperation>> res;
    if (op.get() != nullptr)
      res.push_back(op);
    return res;
  }
  // return a result with lowest cost
  auto minit = std::min_element(cost.begin(), cost.end());
  auto i = std::distance(cost.begin(), minit);
  return results[i];
}

void SingleQueueWorkflowCompiler::compile(RenderWorkflow& workflow)
{
  // verify operations
  verifyOperations(workflow);

  // each compute operation gets its own tag
  // all graphics operations with the same attachment size get the same tag
  // two graphics operations with different attachment size get different tag
  costCalculator.tagOperationByAttachmentType(workflow);

  // resourceOperationRange defines first and last use of a resource
  std::unordered_map<std::string, glm::uvec3> resourceOperationRange;
  // activeResources is a final list of resources used
  std::vector<const WorkflowResource*>        activeResources;
  // activeResourceIndex remaps resource name to its index in activeResources
  std::unordered_map<std::string, uint32_t>   activeResourceIndex;

  // Build a vector storing proper sequence of operations - FIXME : IT ONLY WORKS FOR ONE QUEUE NOW.
  // TARGET FOR THE FUTURE  : build as many operation sequences as there is queues, take VkQueueFlags into consideration
  // ( be aware that scheduling algorithms may be NP-complete ). Also maintain proper synchronization between queues ( events, semaphores )
  std::vector<std::vector<std::shared_ptr<RenderOperation>>> operationSequences;

  {
    std::vector<const WorkflowResource*> resources;

    // FIXME : this should be a loop creating series of command sequences for each existing VkQueue, 
    // but for now we just create a single command sequence for one queue
    {
      // FIXME : IT ONLY GENERATES ONE SEQUENCE NOW, ALSO IGNORES TYPE OF THE QUEUE
      auto operationSequence = recursiveScheduleOperations(workflow, nullptr, &costCalculator);

      // store info about all resources used in an operation sequence
      uint32_t opSeqIndex = 0;
      collectResources(operationSequence, opSeqIndex, resources, resourceOperationRange);
      operationSequences.push_back(operationSequence);
    }
    // find resources that may be aliased by existing resources
    shrinkResources(workflow, resources, activeResources, resourceOperationRange, activeResourceIndex);
  }

  // create frame buffer definitions for all attachments
  std::vector<pumex::FrameBufferImageDefinition> frameBufferDefinitions;
  for (uint32_t i = 0; i < activeResources.size(); ++i)
  {
    const WorkflowResource* res = activeResources[i];
    auto resType = workflow.getResourceType(res->typeName);

    frameBufferDefinitions.push_back(pumex::FrameBufferImageDefinition(
      resType.attachment.attachmentType,
      resType.format,
      0,
      getAspectMask(resType.attachment.attachmentType),
      resType.samples,
      resType.attachment.attachmentSize,
      resType.attachment.swizzles
    ));
  }

  std::vector<std::vector<std::shared_ptr<RenderCommand>>> newCommandSequences;

  // construct render command sequencess ( render passes, compute passes, we don't use events and semaphores yet - these things are for multiqueue operations )
  for( auto& operationSequence : operationSequences )
  {
    std::vector<std::shared_ptr<RenderCommand>> commandSequence = createCommandSequence(operationSequence);
    newCommandSequences.push_back(commandSequence);
  }

  uint32_t operationIndex = 0;
  for ( auto& commandSequence : newCommandSequences )
  {
    std::vector<VkImageLayout> lastLayout(frameBufferDefinitions.size());
    std::fill(lastLayout.begin(), lastLayout.end(), VK_IMAGE_LAYOUT_UNDEFINED);

    for (auto& command : commandSequence)
    {
      switch (command->commandType)
      {
      case RenderCommand::commRenderPass:
      {
        // construct subpasses and subpass dependencies from operations
        std::shared_ptr<RenderPass>              renderPass = std::dynamic_pointer_cast<RenderPass>(command);
        std::vector<LoadOp>                      firstLoadOp(frameBufferDefinitions.size());
        std::vector<SubpassDependencyDefinition> subpassDependencies;
        auto                                     beginLayout  = lastLayout;
        uint32_t                                 passOperationIndex = 0;
        // a list of outputs modified in a current render pass along with the passOperationIndex in which modifications took place
        std::unordered_map<std::string, uint32_t> modifiedOutputs;

        for (auto& operation : renderPass->renderOperations)
        {
          auto subPassDefinition = operation->buildSubPassDefinition(activeResourceIndex);
          renderPass->subpasses.push_back(subPassDefinition);
// default first dependency as defined by the specification - we don't want to use it
//          SubpassDependencyDefinition dependency{ VK_SUBPASS_EXTERNAL, 0, 
//            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 
//            0, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
//            0 }; // VK_DEPENDENCY_BY_REGION_BIT

          auto inputResources = operation->getInputsOutputs(RenderOperation::AllInputs);
          for (auto& inputResource : inputResources)
          {
            uint32_t resIndex = activeResourceIndex[inputResource->name];
            // if this input was generated outside of render pass...
            uint32_t srcSubpass = VK_SUBPASS_EXTERNAL;
            auto it = modifiedOutputs.find(inputResource->name);
            if (it != modifiedOutputs.end())
              srcSubpass = it->second;
            // find subpass dependency that matches one with srcSubpass == srcSubpass and dstSubpass == passOperationIndex
            auto dep = std::find_if(subpassDependencies.begin(), subpassDependencies.end(), [srcSubpass, passOperationIndex](const SubpassDependencyDefinition& sd) -> bool { return sd.srcSubpass == srcSubpass && sd.dstSubpass == passOperationIndex; });
            if (dep == subpassDependencies.end())
              dep = subpassDependencies.insert(subpassDependencies.end(), SubpassDependencyDefinition(srcSubpass, passOperationIndex, 0, 0, 0, 0, 0));
            // add correct flags to the dependency
            dep->srcStageMask  |= 0;
            dep->dstStageMask  |= 0;
            dep->srcAccessMask |= 0;
            dep->dstAccessMask |= 0;
            dep->dependencyFlags |= 0; // dependency by region !!!

            lastLayout[resIndex]                   = inputResource->operationLayout;
            frameBufferDefinitions[resIndex].usage |= getAttachmentUsage(inputResource->operationLayout);
            if( firstLoadOp[resIndex].loadType == LoadOp::DontCare )
              firstLoadOp[resIndex]                = loadOpLoad();
          }

          auto outputResources = operation->getInputsOutputs(RenderOperation::AllOutputs);
          for (auto& outputResource : outputResources)
          {
            uint32_t resIndex = activeResourceIndex[outputResource->name];
            modifiedOutputs[outputResource->name] = passOperationIndex;

            lastLayout[resIndex] = outputResource->operationLayout;
            frameBufferDefinitions[resIndex].usage |= getAttachmentUsage(outputResource->operationLayout);
            if (firstLoadOp[resIndex].loadType == LoadOp::DontCare)
              firstLoadOp[resIndex] = outputResource->loadOperation;
          }

          operationIndex++;
          passOperationIndex++;
        }
        // check if there exist any subpass dependency that has srcSubpass == VK_SUBPASS_EXTERNAL and dstSubpass == 0
        auto dep = std::find_if(subpassDependencies.begin(), subpassDependencies.end(), [](const SubpassDependencyDefinition& sd) -> bool { return sd.srcSubpass == VK_SUBPASS_EXTERNAL && sd.dstSubpass == 0; });
        // if not - add default one, that is empty
        if (dep == subpassDependencies.end())
        {
          SubpassDependencyDefinition introDependency{ VK_SUBPASS_EXTERNAL, 0,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 
            0, 0, 
            0 }; // VK_DEPENDENCY_BY_REGION_BIT
          subpassDependencies.push_back(introDependency);
        }

        // add outro dependency
        // FIXME : this should take into consideration only attachments that will be used in a future
        // now it adds DEFAULT dependency ( as declared in Vulkan specification ) that produces too much burden
        SubpassDependencyDefinition outroDependency{ (uint32_t)renderPass->renderOperations.size()-1, VK_SUBPASS_EXTERNAL,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
          VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0, 
          0 }; // VK_DEPENDENCY_BY_REGION_BIT
        subpassDependencies.push_back(outroDependency);
        renderPass->dependencies = subpassDependencies;

        // construct render pass attachments
        std::vector<AttachmentDefinition> attachmentDefinitions;
        for (uint32_t i = 0; i < activeResources.size(); ++i)
        {
          const WorkflowResource* res = activeResources[i];
          auto resType = workflow.getResourceType(res->typeName);

          bool colorDepthAttachment;
          bool stencilAttachment;
          switch (resType.attachment.attachmentType)
          {
          case atSurface:
          case atColor:
          case atDepth:
            colorDepthAttachment = true;
            stencilAttachment    = false;
            break;
          case atDepthStencil:
            colorDepthAttachment = true;
            stencilAttachment    = true;
            break;
          case atStencil:
            colorDepthAttachment = false;
            stencilAttachment    = true;
            break;
          }

          // resource must be saved when it was tagged as persistent, ot it is a swapchain surface, or it will be used later
          bool mustSaveResource = resType.persistent || 
            resType.attachment.attachmentType == atSurface ||
            resourceOperationRange.at(res->name).y > operationIndex;

          attachmentDefinitions.push_back(AttachmentDefinition(
            i,
            resType.format,
            resType.samples,
            colorDepthAttachment                     ? (VkAttachmentLoadOp)firstLoadOp[i].loadType : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            colorDepthAttachment && mustSaveResource ? VK_ATTACHMENT_STORE_OP_STORE                : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            stencilAttachment                        ? (VkAttachmentLoadOp)firstLoadOp[i].loadType : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            stencilAttachment && mustSaveResource    ? VK_ATTACHMENT_STORE_OP_STORE                : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            beginLayout[i],
            lastLayout[i],
            0
          ));
        }
        renderPass->attachments = attachmentDefinitions;

        break;
      }
      case RenderCommand::commComputePass:
      {
        std::shared_ptr<ComputePass> computePass = std::dynamic_pointer_cast<ComputePass>(command);
        auto& operation = computePass->computeOperation;
        //mangleOP(operation)
        break;
      }
      }
    }
  }
  std::vector<std::shared_ptr<pumex::FrameBuffer>> newFrameBuffers;

  // FIXME : Are old objects still in use by GPU ? May we simply delete them or not ?
  workflow.commandSequences = newCommandSequences;
  workflow.frameBuffers     = newFrameBuffers;
}

void SingleQueueWorkflowCompiler::verifyOperations(RenderWorkflow& workflow)
{
  std::ostringstream os;
  // check if all attachments have the same size in each operation
  for (auto it = workflow.renderOperations.cbegin(); it != workflow.renderOperations.end(); ++it)
  {
    auto opResources = it->second->getInputsOutputs(RenderOperation::AllAttachments);
    std::vector<AttachmentSize> attachmentSizes;
    workflow.getAttachmentSizes(opResources, attachmentSizes);
    bool sameSize = true;
    for (uint32_t i = 0; i < attachmentSizes.size() - 1; ++i)
    {
      if ( attachmentSizes[i] != attachmentSizes[i + 1] )
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

void SingleQueueWorkflowCompiler::collectResources(const std::vector<std::shared_ptr<RenderOperation>>& operationSequence, uint32_t opSeqIndex, std::vector<const WorkflowResource*>& resources, std::unordered_map<std::string, glm::uvec3>& resourceOperationRange)
{
  for (uint32_t i = 0; i < operationSequence.size(); ++i)
  {
    auto opResources = operationSequence[i]->getInputsOutputs(RenderOperation::AllAttachments);
    for (auto resource : opResources)
    {
      // if this is new resource
      auto it = resources.begin();
      while (it != resources.end())
      {
        if ((*it)->name == resource->name)
          break;
        ++it;
      }
      if (it == resources.end())
      {
        resources.push_back(resource);
        resourceOperationRange.insert({ resource->name, glm::uvec3(i,i,opSeqIndex) });
      }
      else
      {
        resourceOperationRange[resource->name].y = i;
      }
    }
  }
}

void SingleQueueWorkflowCompiler::shrinkResources(RenderWorkflow& workflow, const std::vector<const WorkflowResource*>& resources, std::vector<const WorkflowResource*>& newResources, std::unordered_map<std::string, glm::uvec3>& resourceOperationRange, std::unordered_map<std::string, uint32_t>& activeResourceIndex)
{
  // Check if we may shrink the number of resources.
  // Resources are the same when :
  // - their type is the same
  // - their use does not overlap
  std::unordered_map<std::string, std::string> resourceRemap;
  for (auto it0 = resources.begin(); it0 != resources.end(); ++it0)
  {
    // if current resource is already in results then skip it
    if (resourceRemap.find((*it0)->name) != resourceRemap.end())
      continue;
    glm::uvec3 resourceUse0 = resourceOperationRange.at((*it0)->name);
    for (auto it1 = it0+1; it1 != resources.end(); ++it1)
    {
      if ((*it1)->typeName != (*it0)->typeName)
        continue;
      glm::uvec3 resourceUse1 = resourceOperationRange.at((*it1)->name);
      if (resourceUse0.y >= resourceUse1.x)
        continue;
      resourceUse0.y = resourceUse1.y;
      resourceOperationRange[(*it0)->name] = resourceUse0;
      resourceOperationRange[(*it1)->name] = resourceUse0;
      resourceRemap.insert( { (*it1)->name,(*it0)->name } );
    }
  }

  for (auto it0 = resources.begin(); it0 != resources.end(); ++it0)
  {
    bool aliased = false;
    for (auto it1 = resourceRemap.begin(); it1 != resourceRemap.end(); ++it1)
    {
      if (it1->first == (*it0)->name)
      {
        aliased = true;
        break;
      }
    }
    if(!aliased)
      resourceRemap.insert( { (*it0)->name,(*it0)->name } );
  }

  for (auto resource : resources)
  {
    if (resourceRemap.at(resource->name) == resource->name)
      newResources.push_back(resource);
  }
  for (uint32_t i = 0; i < newResources.size(); ++i)
  {
    activeResourceIndex.insert({ newResources[i]->name, i });
    for (auto rm : resourceRemap)
    {
      if (rm.second == newResources[i]->name)
        activeResourceIndex.insert({ rm.first, i });
    }
  }
}



std::vector<std::shared_ptr<RenderCommand>> SingleQueueWorkflowCompiler::createCommandSequence(const std::vector<std::shared_ptr<RenderOperation>>& operationSequence)
{
  std::vector<std::shared_ptr<RenderCommand>> results;

  auto it = operationSequence.begin();
  while (it != operationSequence.end())
  {
    int tag = costCalculator.attachmentTag.at((*it)->name);
    auto bit = it++;
    while (it != operationSequence.end() && (costCalculator.attachmentTag.at((*it)->name) == tag))
      ++it;
    // we have a new set of operations from bit to it
    switch ((*bit)->operationType)
    {
    case RenderOperation::Graphics:
    {
      std::shared_ptr<RenderPass> renderPass = std::make_shared<RenderPass>();
      for (auto xit = bit; xit < it; ++xit)
        renderPass->renderOperations.push_back(*xit);
      results.push_back(renderPass);
      break;
    }
    case RenderOperation::Compute:
    {
      // there is only one compute operation per compute pass
      for (auto xit = bit; xit < it; ++xit)
      {
        std::shared_ptr<ComputePass> computePass = std::make_shared<ComputePass>();
        computePass->computeOperation = std::dynamic_pointer_cast<ComputeOperation>(*xit);
        results.push_back(computePass);
      }
      break;
    }
    default:
      break;
    }
  }
  return results;
}

