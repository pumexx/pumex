//
// Copyright(c) 2017-2018 Paweł Księżopolski ( pumexx )
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
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/FrameBuffer.h>
#include <pumex/RenderPass.h>
#include <pumex/utils/Log.h>

using namespace pumex;

RenderWorkflowResourceType::RenderWorkflowResourceType(const std::string& tn, bool p, VkFormat f, VkSampleCountFlagBits s, AttachmentType at, const AttachmentSize& as)
  : metaType{ Attachment }, typeName{ tn }, persistent{ p }, attachment{ f, s, at, as }
{
}

RenderWorkflowResourceType::RenderWorkflowResourceType(const std::string& tn, bool p)
  : metaType{ Buffer }, typeName{ tn }, persistent{ p }, buffer{}
{

}

WorkflowResource::WorkflowResource(const std::string& n, std::shared_ptr<RenderWorkflowResourceType> t)
  : name{ n }, resourceType{ t }
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

void RenderOperation::setSceneNode(std::shared_ptr<Node> node)
{
  sceneNode = node;
}

ResourceTransition::ResourceTransition(std::shared_ptr<RenderOperation> op, std::shared_ptr<WorkflowResource> res, ResourceTransitionType tt, VkImageLayout l, const LoadOp& ld)
  : operation{ op }, resource{ res }, transitionType{ tt }, attachment{l,ld}
{
}

ResourceTransition::ResourceTransition(std::shared_ptr<RenderOperation> op, std::shared_ptr<WorkflowResource> res, ResourceTransitionType tt, VkPipelineStageFlags ps, VkAccessFlags af)
  : operation{ op }, resource{ res }, transitionType{ tt }, buffer{ps,af}
{
}

ResourceTransition::~ResourceTransition()
{
}

RenderWorkflowSequences::RenderWorkflowSequences(const std::vector<QueueTraits>& qt, const std::vector<std::vector<std::shared_ptr<RenderCommand>>>& com, std::shared_ptr<FrameBufferImages> fbi, std::shared_ptr<RenderPass> orp, uint32_t idx)
  : queueTraits{ qt }, commands{ com }, frameBufferImages{ fbi }, outputRenderPass{ orp }, presentationQueueIndex { idx }
{
}

QueueTraits RenderWorkflowSequences::getPresentationQueue() const
{
  return queueTraits[presentationQueueIndex];
}

RenderWorkflow::RenderWorkflow(const std::string& n, std::shared_ptr<DeviceMemoryAllocator> fba, const std::vector<QueueTraits>& qt)
  : name{ n }, frameBufferAllocator{ fba }, queueTraits{ qt }
{
}

RenderWorkflow::~RenderWorkflow()
{

}

void RenderWorkflow::addResourceType(std::shared_ptr<RenderWorkflowResourceType> tp)
{
  auto it = resourceTypes.find(tp->typeName);
  CHECK_LOG_THROW(it != resourceTypes.end(), "RenderWorkflow : resource type already exists : " + tp->typeName);
  resourceTypes[tp->typeName] = tp;
  valid = false;
}

std::shared_ptr<RenderWorkflowResourceType> RenderWorkflow::getResourceType(const std::string& typeName) const
{
  auto it = resourceTypes.find(typeName);
  CHECK_LOG_THROW(it == resourceTypes.end(), "RenderWorkflow : there is no resource type with name " + typeName);
  return it->second;
}

void RenderWorkflow::addRenderOperation(std::shared_ptr<RenderOperation> op)
{
  auto it = renderOperations.find(op->name);
  CHECK_LOG_THROW(it != renderOperations.end(), "RenderWorkflow : operation already exists : " + op->name);

  op->setRenderWorkflow(shared_from_this());
  renderOperations[op->name] = op;
  valid = false;
}

std::vector<std::string> RenderWorkflow::getRenderOperationNames() const
{
  std::vector<std::string> results;
  for (auto& op : renderOperations)
    results.push_back(op.first);
  return results;
}


std::shared_ptr<RenderOperation> RenderWorkflow::getRenderOperation(const std::string& opName) const
{
  auto it = renderOperations.find(opName);
  CHECK_LOG_THROW(it == renderOperations.end(), "RenderWorkflow : there is no operation with name " + opName);
  return it->second;
}

void RenderWorkflow::setSceneNode(const std::string& opName, std::shared_ptr<Node> node)
{
  getRenderOperation(opName)->sceneNode = node;
  valid = false;
}

std::shared_ptr<Node> RenderWorkflow::getSceneNode(const std::string& opName)
{
  return getRenderOperation(opName)->sceneNode;
}

void RenderWorkflow::addAttachmentInput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkImageLayout layout)
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
  valid = false;
}

void RenderWorkflow::addAttachmentOutput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkImageLayout layout, const LoadOp& loadOp)
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
  valid = false;
}

void RenderWorkflow::addAttachmentResolveOutput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, const std::string& resourceSource, VkImageLayout layout, const LoadOp& loadOp)
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
  std::shared_ptr<ResourceTransition> resourceTransition = std::make_shared<ResourceTransition>(operation, resIt->second, rttAttachmentResolveOutput, layout, loadOp);
  resourceTransition->attachment.resolveResource = resolveIt->second;
  transitions.push_back(resourceTransition);
  valid = false;
}

void RenderWorkflow::addAttachmentDepthOutput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkImageLayout layout, const LoadOp& loadOp)
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
  valid = false;
}

void RenderWorkflow::addBufferInput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkPipelineStageFlagBits pipelineStage, VkAccessFlagBits accessFlags)
{
  auto operation = getRenderOperation(opName);
  auto resType   = getResourceType(resourceType);
  auto resIt     = resources.find(resourceName);
  if (resIt == resources.end())
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
  // FIXME : additional checks
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttBufferInput, pipelineStage, accessFlags));
  valid = false;
}

void RenderWorkflow::addBufferOutput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkPipelineStageFlagBits pipelineStage, VkAccessFlagBits accessFlags)
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
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttBufferOutput, pipelineStage, accessFlags));
  valid = false;
}

std::shared_ptr<WorkflowResource> RenderWorkflow::getResource(const std::string& resourceName) const
{
  auto it = resources.find(resourceName);
  CHECK_LOG_THROW(it == resources.end(), "RenderWorkflow : there is no resource with name " + resourceName);
  return it->second;
}

std::vector<std::string> RenderWorkflow::getResourceNames() const
{
  std::vector<std::string> results;
  for (auto& op : resources)
    results.push_back(op.first);
  return results;
}

void RenderWorkflow::associateResource(const std::string& resourceName, std::shared_ptr<Resource> resource)
{
  auto resIt = resources.find(resourceName);
  CHECK_LOG_THROW(resIt == resources.end(), "RenderWorkflow : cannot associate nonexisting resource");
  associatedResources.insert({ resourceName, resource });
  valid = false;
}

std::shared_ptr<Resource> RenderWorkflow::getAssociatedResource(const std::string& resourceName)
{
  auto resIt = associatedResources.find(resourceName);
  if (resIt == associatedResources.end())
    return std::shared_ptr<Resource>();
  return resIt->second;
}

std::vector<std::shared_ptr<RenderOperation>> RenderWorkflow::getPreviousOperations(const std::string& opName) const
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

std::vector<std::shared_ptr<RenderOperation>> RenderWorkflow::getNextOperations(const std::string& opName) const
{
  auto opTransitions = getOperationIO(opName, rttAllOutputs);

  std::vector<std::shared_ptr<RenderOperation>> nextOperations;
  for (auto opTransition : opTransitions)
  {
    // operation is final if all of its ouputs are inputs for final operations
    auto resTransitions = getResourceIO(opTransition->resource->name, rttAllInputs);
    for (auto resTransition : resTransitions)
      nextOperations.push_back(resTransition->operation);
  }
  return nextOperations;
}

std::vector<std::shared_ptr<ResourceTransition>> RenderWorkflow::getOperationIO(const std::string& opName, ResourceTransitionTypeFlags transitionTypes) const
{
  auto operation = getRenderOperation(opName);
  std::vector<std::shared_ptr<ResourceTransition>> results;
  std::copy_if(transitions.begin(), transitions.end(), std::back_inserter(results),
    [operation, transitionTypes](std::shared_ptr<ResourceTransition> c)->bool{ return c->operation == operation && ( c->transitionType & transitionTypes) ; });
  return results;
}

std::vector<std::shared_ptr<ResourceTransition>> RenderWorkflow::getResourceIO(const std::string& resourceName, ResourceTransitionTypeFlags transitionTypes) const
{
  auto resource = getResource(resourceName);
  std::vector<std::shared_ptr<ResourceTransition>> results;
  std::copy_if(transitions.begin(), transitions.end(), std::back_inserter(results),
    [resource, transitionTypes](std::shared_ptr<ResourceTransition> c)->bool { return c->resource == resource && (c->transitionType & transitionTypes); });
  return results;
}

bool RenderWorkflow::compile(std::shared_ptr<RenderWorkflowCompiler> compiler)
{
  if (valid)
    return false;
  std::lock_guard<std::mutex> lock(compileMutex);
  if (valid)
    return true;
  workflowSequences = compiler->compile(*this);
  valid = true;
  return true;
};

void StandardRenderWorkflowCostCalculator::tagOperationByAttachmentType(const RenderWorkflow& workflow)
{
  std::unordered_map<int, AttachmentSize> tags;
  attachmentTag.clear();
  int currentTag = 0;

  auto operationNames = workflow.getRenderOperationNames();
  for (auto operationName : operationNames)
  {
    auto operation = workflow.getRenderOperation(operationName);
    if (operation->operationType != RenderOperation::Graphics)
    {
      attachmentTag.insert({ operationName, currentTag++ });
      continue;
    }
    auto opTransitions = workflow.getOperationIO(operationName, rttAllAttachments);
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
    attachmentTag.insert({ operationName, tagFound });
  }
}

float StandardRenderWorkflowCostCalculator::calculateWorkflowCost(const RenderWorkflow& workflow, const std::vector<std::shared_ptr<RenderOperation>>& operationSchedule) const
{
  if (operationSchedule.empty())
    return 0.0f;
  float result = 0.0f;

  // first preference : prefer operations with the same tags ( render pass grouping )
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

std::vector<std::shared_ptr<RenderOperation>> recursiveScheduleOperations(const RenderWorkflow& workflow, const std::set<std::shared_ptr<RenderOperation>>& doneOperations, StandardRenderWorkflowCostCalculator* costCalculator)
{
  std::set<std::shared_ptr<RenderOperation>> newDoneOperations;
  auto operationNames = workflow.getRenderOperationNames();
  if (doneOperations.empty())
  {
    for (auto operationName : operationNames)
    {
      auto operation = workflow.getRenderOperation(operationName);
      auto nextOperations = workflow.getNextOperations(operationName);
      if (nextOperations.empty())
        newDoneOperations.insert(operation);
    }
  }
  else
  {
    for (auto operationName : operationNames)
    {
      auto operation = workflow.getRenderOperation(operationName);
      if (doneOperations.find(operation) != doneOperations.end())
        continue;
      auto nextOperations = workflow.getNextOperations(operationName);
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
        newDoneOperations.insert(operation);
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

std::shared_ptr<RenderWorkflowSequences> SingleQueueWorkflowCompiler::compile(RenderWorkflow& workflow)
{
  // verify operations
  verifyOperations(workflow);

  // Tags are used to prefer graphics operations with the same tag value to be performed one after another ( subpass grouping ).
  // Look at calculateWorkflowCost()
  // - each compute operation gets its own tag
  // - all graphics operations with the same attachment size get the same tag
  // - two graphics operations with different attachment size get different tag
  costCalculator.tagOperationByAttachmentType(workflow);

  // collect information about resources used into resourceVector
  std::vector<std::shared_ptr<WorkflowResource>> resourceVector;
  std::unordered_map<std::string, uint32_t>      resourceIndex;
  collectResources(workflow, resourceVector, resourceIndex);

  // build framebuffer definition from resources that are attachments
  std::unordered_map<std::string, uint32_t>      attachmentIndex;
  std::vector<FrameBufferImageDefinition>        frameBufferDefinitions;
  for (uint32_t i = 0; i < resourceVector.size(); ++i)
  {
    auto resourceType = resourceVector[i]->resourceType;
    if (resourceType->metaType != RenderWorkflowResourceType::Attachment)
      continue;

    attachmentIndex.insert({ resourceVector[i]->name, static_cast<uint32_t>(frameBufferDefinitions.size()) });
    frameBufferDefinitions.push_back(FrameBufferImageDefinition(
      resourceType->attachment.attachmentType,
      resourceType->attachment.format,
      0,
      getAspectMask(resourceType->attachment.attachmentType),
      resourceType->attachment.samples,
      resourceVector[i]->name,
      resourceType->attachment.attachmentSize,
      resourceType->attachment.swizzles
    ));
  }

  // Build a vector storing proper sequence of operations - FIXME : IT ONLY WORKS FOR ONE QUEUE NOW.
  // TARGET FOR THE FUTURE  : build as many operation sequences as there is queues, take VkQueueFlags into consideration
  // ( be aware that scheduling algorithms may be NP-complete ).
  std::vector<std::vector<std::shared_ptr<RenderOperation>>> operationSequences;
  {
    // FIXME : IT ONLY GENERATES ONE SEQUENCE NOW, ALSO IGNORES TYPE OF THE QUEUE
    std::set<std::shared_ptr<RenderOperation>> doneOperations;
    auto operationSequence = recursiveScheduleOperations(workflow, doneOperations, &costCalculator);
    operationSequences.push_back(operationSequence);
  }

  // construct render command sequencess ( render passes, compute passes )
  std::vector<std::vector<std::shared_ptr<RenderCommand>>> newCommandSequences;
  for( auto& operationSequence : operationSequences )
  {
    std::vector<std::shared_ptr<RenderCommand>> commandSequence = createCommandSequence(operationSequence, resourceVector, attachmentIndex, frameBufferDefinitions);
    newCommandSequences.push_back(commandSequence);
  }

  // create pipeline barriers
  createPipelineBarriers(workflow, newCommandSequences);

  // find render pass that writes to surface
  uint32_t presentationQueueIndex = 0;
  std::shared_ptr<RenderPass> outputRenderPass;
  for (auto& commandSequence : newCommandSequences)
  {
    for (int i = commandSequence.size()-1; i>=0; --i)
    {
      if (commandSequence[i]->commandType != RenderCommand::ctRenderSubPass)
        continue;
      bool found = false;
      auto transitions = workflow.getOperationIO(commandSequence[i]->operation->name, rttAttachmentOutput | rttAttachmentResolveOutput);
      for (auto transition : transitions)
      {
        if (transition->resource->resourceType->attachment.attachmentType == atSurface)
        {
          found = true;
          break;
        }
      }
      if (found)
      {
        outputRenderPass = commandSequence[i]->asRenderSubPass()->renderPass;
        break;
      }
    }
    if (outputRenderPass.get() != nullptr)
      break;
    presentationQueueIndex++;
  }

  // create frame buffer
  std::shared_ptr<FrameBufferImages> fbi = std::make_shared<FrameBufferImages>(frameBufferDefinitions, workflow.frameBufferAllocator);

  return std::make_shared<RenderWorkflowSequences>(workflow.getQueueTraits(), newCommandSequences, fbi, outputRenderPass, presentationQueueIndex);
}

void SingleQueueWorkflowCompiler::verifyOperations(const RenderWorkflow& workflow)
{
  std::ostringstream os;
  // check if all attachments have the same size in each operation
  auto operationNames = workflow.getRenderOperationNames();
  for (auto operationName : operationNames)
  {
    std::vector<AttachmentSize> attachmentSizes;
    auto opTransitions = workflow.getOperationIO(operationName, rttAllAttachments);
    for (auto transition : opTransitions)
      attachmentSizes.push_back(transition->resource->resourceType->attachment.attachmentSize);
    if (attachmentSizes.empty())
      continue;
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
      os << "Error: Operation <" << operationName << "> : not all attachments have the same size" << std::endl;
    }
  }
  // check if all resources have at most one output that generates them
  auto resourceNames = workflow.getResourceNames();
  for (auto resourceName : resourceNames)
  {
    auto resource = workflow.getResource(resourceName);
    auto opTransitions = workflow.getResourceIO(resourceName, rttAllOutputs);
    if(opTransitions.size()>1)
      os << "Error: Resource <" << resourceName << "> : resource must have at most one output that generates it" << std::endl;
  }

  // if there are some errors - throw exception
  std::string results;
  results = os.str();
  CHECK_LOG_THROW(!results.empty(), "Errors in workflow operations :\n" + results);
}

void SingleQueueWorkflowCompiler::collectResources(const RenderWorkflow& workflow, std::vector<std::shared_ptr<WorkflowResource>>& resourceVector, std::unordered_map<std::string,uint32_t>& resourceIndex)
{
  std::list<std::shared_ptr<RenderOperation>>       nextOperations;
  std::map<std::shared_ptr<WorkflowResource>, bool> resourcesGenerated;
  std::map<std::shared_ptr<WorkflowResource>, bool> resourcesDone;

  resourceVector.clear();
  resourceIndex.clear();

  // put all resources used by workflow onto resourcesGenerated map

  auto operationNames = workflow.getRenderOperationNames();
  for (auto operationName : operationNames)
  {
    auto operation = workflow.getRenderOperation(operationName);
    // First we are looking for operations with no predecessors
    // Such operations will be first in partial ordering, so we put them on nextOperations list.
    // Input resources are most probably sent by CPU, so are marked as generated, but not done.
    // Resource is generated when it first shows in partial ordering ( moved from nextOperations list to sortedOperations )
    // Resource is done when it is no longer used in partial ordering
    auto ops = workflow.getPreviousOperations(operationName);
    if (ops.empty())
    {
      auto opTransitions = workflow.getOperationIO(operationName, rttAllInputs);
      for (auto transition : opTransitions)
      {
        resourcesGenerated.insert({ transition->resource, true });
        // later we will check if these resources are done before first operation
        resourcesDone.insert({ transition->resource, false });
      }
      nextOperations.push_back(operation);
    }

    auto opTransitions = workflow.getOperationIO(operationName, rttAllOutputs);
    for (auto transition : opTransitions)
    {
      resourcesGenerated.insert({ transition->resource,false });
      resourcesDone.insert({ transition->resource,false });
    }
  }
  // Check if all input resources for first operations are done
  // It means that all operations where resource is used as input are currently on nextOperations list.
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
      // output resource is generated by this operation
      resourcesGenerated[transition->resource] = true;

      // Most important part - actual collecting of a resources
      // If there are resources that are done and have the same type - we may use them again
      // if there aren't such resources - we must create a new one
      // Caution - resource may be done and used again, so we must also check if all subsequent uses are done
      if (resourceIndex.find(transition->resource->name) == resourceIndex.end())
      {
        int foundResourceIndex = -1;
        // Resource reuse may only work for attachments - we cannot reuse a resource provided by user
        if (transition->resource->resourceType->metaType == RenderWorkflowResourceType::Attachment)
        {
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

      // for all operations that use this resource as input - push them on nextOperations list if all their inputs are generated
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

std::vector<std::shared_ptr<RenderCommand>> SingleQueueWorkflowCompiler::createCommandSequence(const std::vector<std::shared_ptr<RenderOperation>>& operationSequence, const std::vector<std::shared_ptr<WorkflowResource>>& resourceVector, const std::unordered_map<std::string, uint32_t>& attachmentIndex, std::vector<FrameBufferImageDefinition>& frameBufferDefinitions)
{
  std::vector<std::shared_ptr<RenderCommand>> results;

  int lastTag = -1;
  std::shared_ptr<RenderPass> lastRenderPass;

  std::vector<VkImageLayout> lastLayout(attachmentIndex.size());
  std::fill(lastLayout.begin(), lastLayout.end(), VK_IMAGE_LAYOUT_UNDEFINED);


  for( auto& operation : operationSequence )
  {
    int tag = costCalculator.attachmentTag.at(operation->name);

    // we have a new set of operations from bit to it
    switch (operation->operationType)
    {
    case RenderOperation::Graphics:
    {
      if (lastTag != tag)
      {
        if (lastRenderPass.get() != nullptr)
          lastRenderPass->finalizeAttachments();
        lastRenderPass = std::make_shared<RenderPass>();
        lastRenderPass->initializeAttachments(resourceVector, attachmentIndex, lastLayout);
      }

      std::shared_ptr<RenderSubPass> renderSubPass = std::make_shared<RenderSubPass>();
      renderSubPass->operation = operation;
      renderSubPass->buildSubPassDefinition(attachmentIndex);

      lastRenderPass->addSubPass(renderSubPass);
      lastRenderPass->updateAttachments(renderSubPass, attachmentIndex, lastLayout, frameBufferDefinitions);

      results.push_back(renderSubPass);
      break;
    }
    case RenderOperation::Compute:
    {
      lastRenderPass = nullptr;

      std::shared_ptr<ComputePass> computePass = std::make_shared<ComputePass>();
      computePass->operation = operation;
      results.push_back(computePass);
      break;
    }
    default:
      break;
    }

    lastTag = tag;
  }
  if (lastRenderPass.get() != nullptr)
    lastRenderPass->finalizeAttachments();
  return results;
}

void SingleQueueWorkflowCompiler::createPipelineBarriers(const RenderWorkflow& workflow, std::vector<std::vector<std::shared_ptr<RenderCommand>>>& commandSequences)
{
  std::map<std::string, int> queueNumber;
  std::map<std::string, int> operationNumber;
  std::map<std::string, std::shared_ptr<RenderCommand>> commandMap;
  int queueIndex = 0;
  for (auto& commandSequence : commandSequences)
  {
    int operationIndex = 0;
    for (auto& command : commandSequence)
    {
      queueNumber[command->operation->name]     = queueIndex;
      operationNumber[command->operation->name] = operationIndex;
      commandMap[command->operation->name]      = command;
      operationIndex++;
    }
    queueIndex++;
  }

  auto resourceNames = workflow.getResourceNames();
  for (auto resourceName : resourceNames)
  {
    auto resource = workflow.getResource(resourceName);
    auto generatingTransitions     = workflow.getResourceIO(resourceName, rttAllOutputs);
    if (generatingTransitions.empty())
      continue;

    auto generatingOperationNumber = operationNumber[generatingTransitions[0]->operation->name];
    auto generatingQueueNumber     = queueNumber[generatingTransitions[0]->operation->name];

    // sort consuming transitions according to operation index, operations from current queue will be first in sorted vector
    auto consumingTransitions = workflow.getResourceIO(resourceName, rttAllInputs);
    // place transitions that are in the same queue first
    auto pos = std::partition(consumingTransitions.begin(), consumingTransitions.end(), [&queueNumber, &generatingQueueNumber](std::shared_ptr<ResourceTransition> lhs) 
    {
      return queueNumber[lhs->operation->name] == generatingQueueNumber;
    });
    // sort transitions from the same queue according to order of operations
    std::sort(consumingTransitions.begin(), pos, [&operationNumber](std::shared_ptr<ResourceTransition> lhs, std::shared_ptr<ResourceTransition> rhs)
    {
      return operationNumber[lhs->operation->name] < operationNumber[rhs->operation->name];
    });
    // sort transitions from other queues
    std::sort(pos, consumingTransitions.end(), [&queueNumber, &operationNumber](std::shared_ptr<ResourceTransition> lhs, std::shared_ptr<ResourceTransition> rhs)
    {
      if (queueNumber[lhs->operation->name] == queueNumber[rhs->operation->name])
        return operationNumber[lhs->operation->name] < operationNumber[rhs->operation->name];
      return queueNumber[lhs->operation->name] < queueNumber[rhs->operation->name];
    });

    // for now we will create a barrier/subpass dependency for each transition. It should be later optimized ( some barriers are not necessary )
    for (auto consumingTransition : consumingTransitions)
    {
      if (resource->resourceType->metaType == pumex::RenderWorkflowResourceType::Attachment)
        createSubpassDependency(generatingTransitions[0], commandMap[generatingTransitions[0]->operation->name], consumingTransition, commandMap[consumingTransition->operation->name], queueNumber[generatingTransitions[0]->operation->name], queueNumber[consumingTransition->operation->name]);
      else
        createPipelineBarrier(generatingTransitions[0], commandMap[generatingTransitions[0]->operation->name], consumingTransition, commandMap[consumingTransition->operation->name], queueNumber[generatingTransitions[0]->operation->name], queueNumber[consumingTransition->operation->name]);
    }
  }
}

void SingleQueueWorkflowCompiler::createSubpassDependency(std::shared_ptr<ResourceTransition> generatingTransition, std::shared_ptr<RenderCommand> generatingCommand, std::shared_ptr<ResourceTransition> consumingTransition, std::shared_ptr<RenderCommand> consumingCommand, uint32_t generatingQueueIndex, uint32_t consumingQueueIndex)
{
  VkPipelineStageFlags srcStageMask = 0,  dstStageMask = 0;
  VkAccessFlags        srcAccessMask = 0, dstAccessMask = 0;
  getPipelineStageMasks(generatingTransition, consumingTransition, srcStageMask, dstStageMask);
  getAccessMasks(generatingTransition, consumingTransition, srcAccessMask, dstAccessMask);

  uint32_t             srcSubpassIndex = VK_SUBPASS_EXTERNAL, dstSubpassIndex = VK_SUBPASS_EXTERNAL;
  // try to add subpass dependency to latter command
  if (consumingCommand->commandType == RenderCommand::ctRenderSubPass)
  {
    auto consumingSubpass = std::dynamic_pointer_cast<RenderSubPass>(consumingCommand);
    // if both are render subpasses
    if (generatingCommand->commandType == RenderCommand::ctRenderSubPass)
    {
      auto generatingSubpass = std::dynamic_pointer_cast<RenderSubPass>(generatingCommand);
      // check if it's the same render pass
      if (generatingSubpass->renderPass.get() == consumingSubpass->renderPass.get())
        srcSubpassIndex = generatingSubpass->subpassIndex;
    }
    dstSubpassIndex = consumingSubpass->subpassIndex;

    auto dep = std::find_if(consumingSubpass->renderPass->dependencies.begin(), consumingSubpass->renderPass->dependencies.end(), 
      [srcSubpassIndex, dstSubpassIndex](const SubpassDependencyDefinition& sd) -> bool { return sd.srcSubpass == srcSubpassIndex && sd.dstSubpass == dstSubpassIndex; });
    if (dep == consumingSubpass->renderPass->dependencies.end())
      dep = consumingSubpass->renderPass->dependencies.insert(consumingSubpass->renderPass->dependencies.end(), SubpassDependencyDefinition(srcSubpassIndex, dstSubpassIndex, 0, 0, 0, 0, 0));
    dep->srcStageMask    |= srcStageMask;
    dep->dstStageMask    |= dstStageMask;
    dep->srcAccessMask   |= srcAccessMask;
    dep->dstAccessMask   |= dstAccessMask;
    dep->dependencyFlags |= VK_DEPENDENCY_BY_REGION_BIT;
  }
  else if (generatingCommand->commandType == RenderCommand::ctRenderSubPass) // consumingCommand is not a subpass - let's add it to generating command
  {
    auto generatingSubpass = std::dynamic_pointer_cast<RenderSubPass>(generatingCommand);
    srcSubpassIndex = generatingSubpass->subpassIndex;

    auto dep = std::find_if(generatingSubpass->renderPass->dependencies.begin(), generatingSubpass->renderPass->dependencies.end(),
      [srcSubpassIndex, dstSubpassIndex](const SubpassDependencyDefinition& sd) -> bool { return sd.srcSubpass == srcSubpassIndex && sd.dstSubpass == dstSubpassIndex; });
    if (dep == generatingSubpass->renderPass->dependencies.end())
      dep = generatingSubpass->renderPass->dependencies.insert(generatingSubpass->renderPass->dependencies.end(), SubpassDependencyDefinition(srcSubpassIndex, dstSubpassIndex, 0, 0, 0, 0, 0));
    dep->srcStageMask    |= srcStageMask;
    dep->dstStageMask    |= dstStageMask;
    dep->srcAccessMask   |= srcAccessMask;
    dep->dstAccessMask   |= dstAccessMask;
    dep->dependencyFlags |= VK_DEPENDENCY_BY_REGION_BIT;
  }
  else // none of the commands are subpasses - add pipeline barrier instead
    createPipelineBarrier(generatingTransition, generatingCommand, consumingTransition, consumingCommand, generatingQueueIndex, consumingQueueIndex);
}

void SingleQueueWorkflowCompiler::createPipelineBarrier(std::shared_ptr<ResourceTransition> generatingTransition, std::shared_ptr<RenderCommand> generatingCommand, std::shared_ptr<ResourceTransition> consumingTransition, std::shared_ptr<RenderCommand> consumingCommand, uint32_t generatingQueueIndex, uint32_t consumingQueueIndex)
{
  auto workflow = generatingTransition->operation->renderWorkflow.lock();
  auto resource = workflow->getAssociatedResource(generatingTransition->resource->name);
  if (resource.get() == nullptr)
    return;

  uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  if (generatingQueueIndex != consumingQueueIndex)
  {
    // FIXME - find family indices for both queues
  }

  VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (generatingTransition->transitionType != rttBufferInput && generatingTransition->transitionType != rttBufferOutput)
    oldLayout = generatingTransition->attachment.layout;
  if (consumingTransition->transitionType != rttBufferInput && consumingTransition->transitionType != rttBufferOutput)
    newLayout = consumingTransition->attachment.layout;

  VkPipelineStageFlags srcStageMask = 0,  dstStageMask = 0;
  VkAccessFlags        srcAccessMask = 0, dstAccessMask = 0;
  getPipelineStageMasks(generatingTransition, consumingTransition, srcStageMask, dstStageMask);
  getAccessMasks(generatingTransition, consumingTransition, srcAccessMask, dstAccessMask);

  VkDependencyFlags dependencyFlags = 0; // FIXME

  ResourceBarrierGroup rbg(srcStageMask, dstStageMask, dependencyFlags);
  auto rbgit = consumingCommand->barriersBeforeOp.find(rbg);
  if (rbgit == consumingCommand->barriersBeforeOp.end())
    rbgit = consumingCommand->barriersBeforeOp.insert({ rbg, std::vector<ResourceBarrier>() }).first;
  rbgit->second.push_back(ResourceBarrier(resource, srcAccessMask, dstAccessMask, srcQueueFamilyIndex, dstQueueFamilyIndex, oldLayout, newLayout));
}
