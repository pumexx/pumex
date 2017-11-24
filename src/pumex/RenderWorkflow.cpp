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

RenderWorkflowResourceType::RenderWorkflowResourceType(const std::string& tn, VkFormat f, VkSampleCountFlagBits s, bool p, AttachmentType at, const AttachmentSize& as)
  : metaType{ Attachment }, typeName{ tn }, format{ f }, samples{ s }, persistent{ p }, attachment{ at, as }
{
}


WorkflowResource::WorkflowResource(const std::string& n, std::shared_ptr<RenderWorkflowResourceType> t)
  : name{ n }, resourceType{ t }
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

void RenderOperation::setRenderWorkflow ( std::shared_ptr<RenderWorkflow> rw )
{
  renderWorkflow = rw;
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

SubpassDefinition RenderOperation::buildSubPassDefinition(const std::unordered_map<std::string, uint32_t>& resourceIndex) const
{
  // Fun fact : VkSubpassDescription with compute bind point is forbidden by Vulkan spec
  VkPipelineBindPoint              bindPoint = (operationType == Graphics) ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;
  std::vector<AttachmentReference> ia;
  std::vector<AttachmentReference> oa;
  std::vector<AttachmentReference> ra;
  AttachmentReference              dsa;
  std::vector<uint32_t>            pa;

  std::shared_ptr<RenderWorkflow> rw = renderWorkflow.lock();
  auto inputAttachments   = rw->getOperationIO(name, rttAttachmentInput);
  auto outputAttachments  = rw->getOperationIO(name, rttAttachmentOutput);
  auto resolveAttachments = rw->getOperationIO(name, rttAttachmentResolveOutput);
  auto depthAttachments   = rw->getOperationIO(name, rttAttachmentDepthOutput);

  for (auto inputAttachment : inputAttachments)
    ia.push_back({ resourceIndex.at(inputAttachment->resource->name), inputAttachment->layout });
  for (auto outputAttachment : outputAttachments)
  {
    oa.push_back({ resourceIndex.at(outputAttachment->resource->name), outputAttachment->layout });

    if (!resolveAttachments.empty())
    {
      auto it = std::find_if(resolveAttachments.begin(), resolveAttachments.end(), [outputAttachment](const std::shared_ptr<ResourceTransition>& rt) -> bool { return rt->resolveResource == outputAttachment->resource; });
      if (it != resolveAttachments.end())
        ra.push_back({ resourceIndex.at( (*it)->resolveResource->name), (*it)->layout });
      else
        ra.push_back({ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
    }
  }
  if (!depthAttachments.empty())
    dsa = { resourceIndex.at(depthAttachments[0]->resource->name), depthAttachments[0]->layout };
  else
    dsa = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
  return SubpassDefinition(bindPoint, ia, oa, ra, dsa, pa);
}

ResourceTransition::ResourceTransition(std::shared_ptr<RenderOperation> op, std::shared_ptr<WorkflowResource> res, ResourceTransitionType tt, VkImageLayout l, const LoadOp& lo)
  : operation{ op }, resource{ res }, transitionType{ tt }, resolveResource{}, layout{ l }, load { lo }
{

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

void RenderWorkflow::addResourceType(std::shared_ptr<RenderWorkflowResourceType> tp)
{
  resourceTypes[tp->typeName] = tp;
}

std::shared_ptr<RenderWorkflowResourceType> RenderWorkflow::getResourceType(const std::string& typeName) const
{
  auto it = resourceTypes.find(typeName);
  CHECK_LOG_THROW(it == resourceTypes.end(), "RenderWorkflow : there is no resource type with name " + typeName);
  return it->second;
}

void RenderWorkflow::addRenderOperation(std::shared_ptr<RenderOperation> op)
{
  op->setRenderWorkflow(shared_from_this());
  renderOperations[op->name] = op;
}


std::shared_ptr<RenderOperation> RenderWorkflow::getRenderOperation(const std::string& opName) const
{
  auto it = renderOperations.find(opName);
  CHECK_LOG_THROW(it == renderOperations.end(), "RenderWorkflow : there is no operation with name " + opName);
  return it->second;
}

std::shared_ptr<WorkflowResource> RenderWorkflow::getResource(const std::string& resourceName) const
{
  auto it = resources.find(resourceName);
  CHECK_LOG_THROW(it == resources.end(), "RenderWorkflow : there is no resource with name " + resourceName);
  return it->second;
}


void RenderWorkflow::addAttachmentInput(const std::string& opName, const std::string& resourceName, const std::string& resourceType, VkImageLayout layout)
{
  auto operation = getRenderOperation(opName);
  auto resType   = getResourceType(resourceType);
  auto resIt     = resources.find(resourceName);
  if (resIt == resources.end())
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
  // FIXME : additional checks
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttAttachmentInput, layout, loadOpLoad()));
}

void RenderWorkflow::addAttachmentOutput(const std::string& opName, const std::string& resourceName, const std::string& resourceType, VkImageLayout layout, const LoadOp& loadOp)
{
  auto operation = getRenderOperation(opName);
  auto resType   = getResourceType(resourceType);
  auto resIt     = resources.find(resourceName);
  if (resIt == resources.end())
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
  {
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
    // resource may only have one transition with output type
  }
  // FIXME : additional checks
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttAttachmentOutput, layout, loadOp));
}

void RenderWorkflow::addAttachmentResolveOutput(const std::string& opName, const std::string& resourceName, const std::string& resourceType, const std::string& resourceSource, VkImageLayout layout, const LoadOp& loadOp)
{
  auto operation = getRenderOperation(opName);
  auto resType   = getResourceType(resourceType);
  auto resIt     = resources.find(resourceName);
  if (resIt == resources.end())
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
  {
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
    // resource may only have one transition with output type
  }
  auto resolveIt = resources.find(resourceSource);
  CHECK_LOG_THROW(resolveIt == resources.end(), "RenderWorkflow : added pointer no to nonexisting resolve resource")

  // FIXME : additional checks
  std::shared_ptr<ResourceTransition> resourceTransition = std::make_shared<ResourceTransition>(operation, resIt->second, rttAttachmentOutput, layout, loadOp);
  resourceTransition->resolveResource = resolveIt->second;
  transitions.push_back(resourceTransition);
}

void RenderWorkflow::addAttachmentDepthOutput(const std::string& opName, const std::string& resourceName, const std::string& resourceType, VkImageLayout layout, const LoadOp& loadOp)
{
  auto operation = getRenderOperation(opName);
  auto resType   = getResourceType(resourceType);
  auto resIt     = resources.find(resourceName);
  if (resIt == resources.end())
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
  {
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
    // resource may only have one transition with output type
  }
  // FIXME : additional checks
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttAttachmentDepthOutput, layout, loadOp));

}

std::vector<std::shared_ptr<RenderOperation>> RenderWorkflow::getPreviousOperations(const std::string& opName)
{
  auto opTransitions = getOperationIO(opName, rttAllInputs);

  std::vector<std::shared_ptr<RenderOperation>> previousOperations;
  for (auto opTransition : opTransitions)
  {
    // operation is final if all of its ouputs are inputs for final operations
    auto resTransitions = getResourceIO(opTransition->resource->name, rttAllOutputs);
    for (auto resTransition : resTransitions)
      previousOperations.push_back(resTransition->operation);
  }
  return previousOperations;
}

std::vector<std::shared_ptr<RenderOperation>> RenderWorkflow::getNextOperations(const std::string& opName)
{
  auto opTransitions = getOperationIO(opName, rttAllOutputs);

  std::vector<std::shared_ptr<RenderOperation>> previousOperations;
  for (auto opTransition : opTransitions)
  {
    // operation is final if all of its ouputs are inputs for final operations
    auto resTransitions = getResourceIO(opTransition->resource->name, rttAllInputs);
    for (auto resTransition : resTransitions)
      previousOperations.push_back(resTransition->operation);
  }
  return previousOperations;
}


std::vector<std::shared_ptr<ResourceTransition>> RenderWorkflow::getOperationIO(const std::string& opName, ResourceTransitionType transitionTypes) const
{
  auto operation = getRenderOperation(opName);
  std::vector<std::shared_ptr<ResourceTransition>> results;
  std::copy_if(transitions.begin(), transitions.end(), std::back_inserter(results),
    [operation, transitionTypes](std::shared_ptr<ResourceTransition> c)->bool{ return c->operation == operation && ( c->transitionType & transitionTypes) ; });
  return results;
}

std::vector<std::shared_ptr<ResourceTransition>> RenderWorkflow::getResourceIO(const std::string& resourceName, ResourceTransitionType transitionTypes) const
{
  auto resource = getResource(resourceName);
  std::vector<std::shared_ptr<ResourceTransition>> results;
  std::copy_if(transitions.begin(), transitions.end(), std::back_inserter(results),
    [resource, transitionTypes](std::shared_ptr<ResourceTransition> c)->bool { return c->resource == resource && (c->transitionType & transitionTypes); });
  return results;
}

void RenderWorkflow::addQueue(const QueueTraits& qt)
{
  queueTraits.push_back(qt);
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
  for (auto operation : workflow.renderOperations)
  {
    if (operation.second->operationType != RenderOperation::Graphics)
    {
      attachmentTag.insert({ operation.first, currentTag++ });
      continue;
    }
    auto opTransitions = workflow.getOperationIO(operation.first, rttAllAttachments);
    AttachmentSize attachmentSize;
    // operations have the same sizes - just take the first one
    AttachmentSize atSize = opTransitions.empty() ? AttachmentSize() : opTransitions[0]->resource->resourceType->attachment.attachmentSize;
    int tagFound = -1;
    for (auto tit : tags)
    {
      if (tit.second == attachmentSize)
      {
        tagFound = tit.first;
        break;
      }
    }
    if (tagFound < 0)
    {
      tagFound = currentTag++;
      tags.insert({ tagFound, attachmentSize });
    }
    attachmentTag.insert({ operation.first, tagFound });
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

std::vector<std::shared_ptr<RenderOperation>> recursiveScheduleOperations(RenderWorkflow workflow, const std::set<std::shared_ptr<RenderOperation>>& doneOperations, StandardRenderWorkflowCostCalculator* costCalculator)
{
  std::set<std::shared_ptr<RenderOperation>> newDoneOperations;
  if (doneOperations.empty())
  {
    for (auto operation : workflow.renderOperations)
    {
      auto nextOperations = workflow.getNextOperations(operation.first);
      if (nextOperations.empty())
        newDoneOperations.insert(operation.second);
    }
  }
  else
  {
    for (auto operation : workflow.renderOperations)
    {
      if (doneOperations.find(operation.second) != doneOperations.end())
        continue;
      auto nextOperations = workflow.getNextOperations(operation.first);
      bool final = true;
      // check if ALL outputs point at operations in doneOperations
      for (auto nextOp : nextOperations)
      {
        if (doneOperations.find(nextOp) == doneOperations.end())
        {
          final = false;
          break;
        }
      }
      if (final)
        newDoneOperations.insert(operation.second);
    }
  }
  if (newDoneOperations.empty())
    return std::vector<std::shared_ptr<RenderOperation>>();

  std::vector<std::vector<std::shared_ptr<RenderOperation>>> results;
  std::vector<float> cost;
  for (const auto& x : newDoneOperations)
  {
    auto a = doneOperations;
    a.insert(x);
    auto xx = recursiveScheduleOperations(workflow, a, costCalculator);
    xx.push_back(x);
    cost.push_back(costCalculator->calculateWorkflowCost(workflow, xx));
    results.push_back(xx);
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

  // Build a vector storing proper sequence of operations - FIXME : IT ONLY WORKS FOR ONE QUEUE NOW.
  // TARGET FOR THE FUTURE  : build as many operation sequences as there is queues, take VkQueueFlags into consideration
  // ( be aware that scheduling algorithms may be NP-complete ). Also maintain proper synchronization between queues ( events, semaphores )
  std::vector<std::vector<std::shared_ptr<RenderOperation>>> operationSequences;
  {
    // FIXME : IT ONLY GENERATES ONE SEQUENCE NOW, ALSO IGNORES TYPE OF THE QUEUE
    std::set<std::shared_ptr<RenderOperation>> doneOperations;
    auto operationSequence = recursiveScheduleOperations(workflow, doneOperations, &costCalculator);
    operationSequences.push_back(operationSequence);
  }

  std::vector<std::shared_ptr<WorkflowResource>> resourceVector;
  std::unordered_map<std::string, uint32_t>      resourceIndex;
  collectResources(workflow, resourceVector, resourceIndex);

  std::unordered_map<std::string, uint32_t>      resourceIndexs;

  // FIXME : resource vector has ALL inputs - we only need attachments to create framebuffer
  std::vector<pumex::FrameBufferImageDefinition> frameBufferDefinitions;
  for (uint32_t i = 0; i < resourceVector.size(); ++i)
  {
    auto resourceType = resourceVector[i]->resourceType;

    frameBufferDefinitions.push_back(pumex::FrameBufferImageDefinition(
      resourceType->attachment.attachmentType,
      resourceType->format,
      0,
      getAspectMask(resourceType->attachment.attachmentType),
      resourceType->samples,
      resourceType->attachment.attachmentSize,
      resourceType->attachment.swizzles
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

        // collect a set of operations that are run after current render pass
        std::set<std::string> resourcesUsedAfterRenderPass;
        for (auto& operation : renderPass->renderOperations)
        {
          auto nextOperations = workflow.getNextOperations(operation->name);
          while (!nextOperations.empty())
          {
            std::set<std::shared_ptr<RenderOperation>> laterOperations;
            for (auto nextOperation : nextOperations)
            {
              if (std::find(renderPass->renderOperations.begin(), renderPass->renderOperations.end(), nextOperation) == renderPass->renderOperations.end())
              {
                auto inputTransitions = workflow.getOperationIO(nextOperation->name, rttAllInputs);
                for (auto inputTransition : inputTransitions)
                  resourcesUsedAfterRenderPass.insert(inputTransition->resource->name);
                auto laterOps = workflow.getNextOperations(nextOperation->name);
                for (auto lop : laterOps)
                  laterOperations.insert(lop);
              }
            }
            nextOperations.clear();
            std::copy(laterOperations.begin(), laterOperations.end(), std::back_inserter(nextOperations));
          }
        }

        // build subpasses, attachment definitions, dependencies, pipeline barriers and events
        for (auto& operation : renderPass->renderOperations)
        {
          auto subPassDefinition = operation->buildSubPassDefinition(resourceIndex);
          renderPass->subpasses.push_back(subPassDefinition);

// default first dependency as defined by the specification - we don't want to use it
//          SubpassDependencyDefinition dependency{ VK_SUBPASS_EXTERNAL, 0, 
//            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 
//            0, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
//            0 }; // VK_DEPENDENCY_BY_REGION_BIT

          auto inputTransitions = workflow.getOperationIO(operation->name, rttAllInputs);
          for (auto inputTransition : inputTransitions)
          {
            // find operation that generated this input
            auto generateTransitions = workflow.getResourceIO(inputTransition->resource->name, rttAllOutputs);
            std::shared_ptr<ResourceTransition> generatingTransition;
            for (auto gen : generateTransitions)
            {
              if (gen->resource == inputTransition->resource)
                generatingTransition = gen;
            }
            uint32_t resIndex = resourceIndex[inputTransition->resource->name];
            // if this input was generated outside of render pass...
            uint32_t srcSubpass = VK_SUBPASS_EXTERNAL;
            auto it = modifiedOutputs.find(inputTransition->resource->name);
            if (it != modifiedOutputs.end())
              srcSubpass = it->second;
            // find subpass dependency that matches one with srcSubpass == srcSubpass and dstSubpass == passOperationIndex
            auto dep = std::find_if(subpassDependencies.begin(), subpassDependencies.end(), [srcSubpass, passOperationIndex](const SubpassDependencyDefinition& sd) -> bool { return sd.srcSubpass == srcSubpass && sd.dstSubpass == passOperationIndex; });
            if (dep == subpassDependencies.end())
              dep = subpassDependencies.insert(subpassDependencies.end(), SubpassDependencyDefinition(srcSubpass, passOperationIndex, 0, 0, 0, 0, 0));
            // add correct flags to the dependency
            VkPipelineStageFlags    srcStageMask = 0, dstStageMask = 0;
            VkAccessFlags           srcAccessMask = 0, dstAccessMask = 0;
            getPipelineStageMasks(generatingTransition, inputTransition, srcStageMask, dstStageMask);
            getAccessMasks(generatingTransition, inputTransition, srcAccessMask, dstAccessMask);
            dep->srcStageMask  |= srcStageMask;
            dep->dstStageMask  |= dstStageMask;
            dep->srcAccessMask |= srcAccessMask;
            dep->dstAccessMask |= dstAccessMask;
            // if input resource is an attachment - set VK_DEPENDENCY_BY_REGION_BIT ( FIXME ?!? )
            if( inputTransition->resource->resourceType->metaType == RenderWorkflowResourceType::Attachment )
              dep->dependencyFlags |= VK_DEPENDENCY_BY_REGION_BIT;

            lastLayout[resIndex]                   = inputTransition->layout;
            frameBufferDefinitions[resIndex].usage |= getAttachmentUsage(inputTransition->layout);
            if( firstLoadOp[resIndex].loadType == LoadOp::DontCare )
              firstLoadOp[resIndex]                = loadOpLoad();

            // FIXME : if generating transition and input transition point to operations in different queues, then we need to add synchronizing EVENT
          }

          auto outputTransitions = workflow.getOperationIO(operation->name, rttAllOutputs);
          for (auto outputTransition : outputTransitions)
          {
            uint32_t resIndex = resourceIndex[outputTransition->resource->name];
            modifiedOutputs[outputTransition->resource->name] = passOperationIndex;

            lastLayout[resIndex] = outputTransition->layout;
            frameBufferDefinitions[resIndex].usage |= getAttachmentUsage(outputTransition->layout);
            if (firstLoadOp[resIndex].loadType == LoadOp::DontCare)
              firstLoadOp[resIndex] = outputTransition->load;
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
        for (uint32_t i = 0; i < resourceVector.size(); ++i)
        {
          auto res = resourceVector[i];
          auto resType = res->resourceType;

          bool colorDepthAttachment;
          bool stencilAttachment;
          switch (resType->attachment.attachmentType)
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

          // Resource must be saved when it was tagged as persistent, ot it is a swapchain surface  or it will be used later
          // FIXME : resource will be used later if any of its subsequent uses has loadOpLoad or it is used as input - THESE CONDITIONS ARE NOT EXAMINED AT THE MOMENT
          bool mustSaveResource = resType->persistent ||
            resType->attachment.attachmentType == atSurface ||
            resourcesUsedAfterRenderPass.find(res->name) != resourcesUsedAfterRenderPass.end();

          attachmentDefinitions.push_back(AttachmentDefinition(
            i,
            resType->format,
            resType->samples,
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
  for (auto operation : workflow.renderOperations)
  {
    std::vector<AttachmentSize> attachmentSizes;
    auto opTransitions = workflow.getOperationIO(operation.first, rttAllAttachments);
    for (auto transition : opTransitions)
      attachmentSizes.push_back(transition->resource->resourceType->attachment.attachmentSize);
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
      os << "Error: Operation <" << operation.first << "> : not all attachments have the same size" << std::endl;
    }
  }
  // check if all resources have at most one output that generates them
  for (auto resource : workflow.resources)
  {
    auto opTransitions = workflow.getResourceIO(resource.first, rttAllOutputs);
    if(opTransitions.size()>1)
      os << "Error: Resource <" << resource.first << "> : resource must have at most one output that generates it" << std::endl;
  }

  // if there are some errors - throw exception
  std::string results;
  results = os.str();
  CHECK_LOG_THROW(!results.empty(), "Errors in workflow operations :\n" + results);
}

void SingleQueueWorkflowCompiler::collectResources(RenderWorkflow& workflow, std::vector<std::shared_ptr<WorkflowResource>>& resourceVector, std::unordered_map<std::string,uint32_t>& resourceIndex)
{
  std::list<std::shared_ptr<RenderOperation>>       nextOperations;
  std::map<std::shared_ptr<WorkflowResource>, bool> resourcesGenerated;
  std::map<std::shared_ptr<WorkflowResource>, bool> resourcesDone;

  resourceVector.clear();
  resourceIndex.clear();

  // put all resources used by workflow onto allUsedResources map
  for (auto operation : workflow.renderOperations)
  {
    auto ops = workflow.getPreviousOperations(operation.first);
    if (ops.empty())
    {
      auto opTransitions = workflow.getOperationIO(operation.first, rttAllInputs);
      for (auto transition : opTransitions)
      {
        resourcesGenerated.insert({ transition->resource,true });
        // later we will check if these resources are done before first operation
        resourcesDone.insert({ transition->resource,false });
      }
      nextOperations.push_back(operation.second);
    }
    auto opTransitions = workflow.getOperationIO(operation.first, rttAllOutputs);
    for (auto transition : opTransitions)
    {
      resourcesGenerated.insert({ transition->resource,false });
      resourcesDone.insert({ transition->resource,false });
    }
  }
  // additionally - check if all resources for first operations are done
  for (auto resource : resourcesGenerated)
  {
    if (resource.second)
    {
      auto resTransitions = workflow.getResourceIO(resource.first->name, rttAllInputs);
      bool done = true;
      for (auto transition : resTransitions)
      {
        if (std::find(nextOperations.begin(), nextOperations.end(), transition->operation) == nextOperations.end())
        {
          done = false;
          break;
        }
      }
      resourcesDone[resource.first] = done;
    }
  }

  std::vector<std::tuple<std::string, std::string>> results;
  std::vector<std::shared_ptr<RenderOperation>> sortedOperations;
  while (!nextOperations.empty())
  {
    auto operation = nextOperations.front();
    nextOperations.pop_front();
    sortedOperations.push_back(operation);

    auto outTransitions = workflow.getOperationIO(operation->name, rttAllOutputs);
    for (auto transition : outTransitions)
    {
      // resource is generated by operation
      resourcesGenerated[transition->resource] = true;

      // Most important part - actual collecting of a resources
      // If there are resources that are done and have the same type - we may use them again
      // if there aren't such resources - we must create a new one
      // Caution - resource may be done and used again, so we must also check if all subsequent uses are done
      if (resourceIndex.find(transition->resource->name) == resourceIndex.end())
      {
        int foundResourceIndex = -1;
        for (auto res : resourceIndex)
        {
          auto examinedResource = workflow.getResource(res.first);
          auto examinedResourceIndex = res.second;
          // examined resource must be done
          if (!resourcesDone[examinedResource])
            continue;
          // examined resource must have the same type as transition->resource
          if (transition->resource->resourceType->typeName != examinedResource->resourceType->typeName)
            continue;
          // find all aliased resources that use it
          // if any of them is not done - we cannot alias it
          bool allResourcesDone = true;
          for (auto res2 : resourceIndex)
          {
            if (res2.second != res.second)
              continue;
            auto examinedResource2 = workflow.getResource(res2.first);
            if (!resourcesDone[examinedResource2])
            {
              allResourcesDone = false;
              break;
            }
          }
          if (!allResourcesDone)
            continue;
          foundResourceIndex = examinedResourceIndex;
        }
        // if no matching resource have been found
        if (foundResourceIndex >= 0)
        {
          resourceIndex.insert({ transition->resource->name ,foundResourceIndex });
        }
        else
        {
          resourceIndex.insert({ transition->resource->name ,(uint32_t)resourceVector.size() });
          resourceVector.push_back(transition->resource);
        }
      }

      // for all operations that use this resource - push them on nextOperations list if all their inputs are generated
      auto resTransitions = workflow.getResourceIO(transition->resource->name, rttAllInputs);
      for (auto transition : resTransitions)
      {
        auto prevResources = workflow.getResourceIO(transition->resource->name, rttAllInputs);
        bool allResourcesGenerated = true;
        for (auto prevRes : prevResources)
        {
          if (!resourcesGenerated.at(prevRes->resource))
          {
            allResourcesGenerated = false;
            break;
          }
        }
        if (allResourcesGenerated)
          nextOperations.push_back(transition->operation);
      }
    }
    // check if resource is done ( all operations that use this operation as input - are on sortedOperations vector )
    auto inTransitions = workflow.getOperationIO(operation->name, rttAllInputs);
    bool done = true;
    for (auto transition : inTransitions)
    {
      if (std::find(sortedOperations.begin(), sortedOperations.end(), transition->operation) == sortedOperations.end())
      {
        done = false;
        break;
      }
      resourcesDone[transition->resource] = done;
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

