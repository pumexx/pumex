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

RenderWorkflowResourceType::RenderWorkflowResourceType(const std::string& tn, bool p, VkFormat f, VkSampleCountFlagBits s, AttachmentType at, const AttachmentSize& as, VkImageUsageFlags iu)
  : metaType{ Attachment }, typeName{ tn }, persistent{ p }, attachment{ f, s, at, as, iu }
{
}

RenderWorkflowResourceType::RenderWorkflowResourceType(const std::string& tn, bool p, const MetaType& mt)
  : metaType{ mt }, typeName{ tn }, persistent{ p }
{
  CHECK_LOG_THROW(metaType == Attachment, "RenderWorkflowResourceType() : Cannot create attachment using this constructor");
}

bool RenderWorkflowResourceType::isEqual(const RenderWorkflowResourceType& rhs) const
{
  // resources must have the same metatype
  if (metaType != rhs.metaType)
    return false;
  switch (metaType)
  {
  case Attachment:
    return attachment.isEqual(rhs.attachment);
  case Buffer:
  case Image:
  default:
    return false;
  }
  return false;
}

bool RenderWorkflowResourceType::AttachmentData::isEqual(const AttachmentData& rhs) const
{
  if (format != rhs.format)
    return false;
  if (samples != rhs.samples)
    return false;
  if (attachmentType != rhs.attachmentType)
    return false;
  if (attachmentSize != rhs.attachmentSize)
    return false;
  // swizzles ?!?
  return true;
}

WorkflowResource::WorkflowResource(const std::string& n, std::shared_ptr<RenderWorkflowResourceType> t)
  : name{ n }, resourceType{ t }
{
}

RenderOperation::RenderOperation(const std::string& n, RenderOperation::Type t, AttachmentSize at, VkSubpassContents sc)
  : name{ n }, operationType{ t }, attachmentSize{ at }, subpassContents { sc }
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

ResourceTransition::ResourceTransition(std::shared_ptr<RenderOperation> op, std::shared_ptr<WorkflowResource> res, ResourceTransitionType tt, VkPipelineStageFlags ps, VkAccessFlags af, const BufferSubresourceRange& bsr)
  : operation{ op }, resource{ res }, transitionType{ tt }, buffer{ps,af, bsr}
{
}

ResourceTransition::ResourceTransition(std::shared_ptr<RenderOperation> op, std::shared_ptr<WorkflowResource> res, ResourceTransitionType tt, VkImageLayout l, const LoadOp& ld, const ImageSubresourceRange& isr)
  : operation{ op }, resource{ res }, transitionType{ tt }, image{ l,ld, isr }
{
}


ResourceTransition::~ResourceTransition()
{
}

RenderWorkflowSequences::RenderWorkflowSequences(const std::vector<QueueTraits>& qt, const std::vector<std::vector<std::shared_ptr<RenderCommand>>>& com, std::shared_ptr<FrameBuffer> fb, const std::vector<VkImageLayout>& iil, std::shared_ptr<RenderPass> orp, uint32_t idx)
  : queueTraits{ qt }, commands{ com }, frameBuffer{ fb }, initialImageLayouts{ iil }, outputRenderPass { orp }, presentationQueueIndex{ idx }
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
  CHECK_LOG_THROW(it != end(resourceTypes), "RenderWorkflow : resource type already exists : " + tp->typeName);
  resourceTypes[tp->typeName] = tp;
  valid = false;
}

void RenderWorkflow::addResourceType(const std::string& typeName, bool persistent, VkFormat format, VkSampleCountFlagBits samples, AttachmentType attachmentType, const AttachmentSize& attachmentSize, VkImageUsageFlags imageUsage)
{
  addResourceType(std::make_shared<RenderWorkflowResourceType>(typeName, persistent, format, samples, attachmentType, attachmentSize, imageUsage));
}

void RenderWorkflow::addResourceType(const std::string& typeName, bool persistent, const RenderWorkflowResourceType::MetaType& metaType)
{
  addResourceType(std::make_shared<RenderWorkflowResourceType>(typeName, persistent, metaType));
}

std::shared_ptr<RenderWorkflowResourceType> RenderWorkflow::getResourceType(const std::string& typeName) const
{
  auto it = resourceTypes.find(typeName);
  CHECK_LOG_THROW(it == end(resourceTypes), "RenderWorkflow : there is no resource type with name " + typeName);
  return it->second;
}

void RenderWorkflow::addRenderOperation(std::shared_ptr<RenderOperation> op)
{
  auto it = renderOperations.find(op->name);
  CHECK_LOG_THROW(it != end(renderOperations), "RenderWorkflow : operation already exists : " + op->name);

  op->setRenderWorkflow(shared_from_this());
  renderOperations[op->name] = op;
  valid = false;
}

void RenderWorkflow::addRenderOperation(const std::string& name, RenderOperation::Type operationType, AttachmentSize attachmentSize, VkSubpassContents subpassContents)
{
  addRenderOperation(std::make_shared<RenderOperation>(name, operationType, attachmentSize, subpassContents));
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
  CHECK_LOG_THROW(it == end(renderOperations), "RenderWorkflow : there is no operation with name " + opName);
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
  if (resIt == end(resources))
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
  CHECK_LOG_THROW(resType->metaType != RenderWorkflowResourceType::Attachment, "RenderWorkflow::addAttachmentInput() : resource is not an attachment");
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttAttachmentInput, layout, loadOpLoad()));
  valid = false;
}

void RenderWorkflow::addAttachmentOutput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkImageLayout layout, const LoadOp& loadOp)
{
  auto operation = getRenderOperation(opName);
  auto resType   = getResourceType(resourceType);
  auto resIt     = resources.find(resourceName);
  if (resIt == end(resources))
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
  {
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
    // resource may only have one transition with output type
  }
  CHECK_LOG_THROW(resType->metaType != RenderWorkflowResourceType::Attachment, "RenderWorkflow::addAttachmentOutput() : resource is not an attachment");
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttAttachmentOutput, layout, loadOp));
  valid = false;
}

void RenderWorkflow::addAttachmentResolveOutput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, const std::string& resourceSource, VkImageLayout layout, const LoadOp& loadOp)
{
  auto operation = getRenderOperation(opName);
  auto resType   = getResourceType(resourceType);
  auto resIt     = resources.find(resourceName);
  if (resIt == end(resources))
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
  {
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
    // resource may only have one transition with output type
  }
  auto resolveIt = resources.find(resourceSource);
  CHECK_LOG_THROW(resolveIt == end(resources), "RenderWorkflow : added pointer no to nonexisting resolve resource");
  CHECK_LOG_THROW(resType->metaType != RenderWorkflowResourceType::Attachment, "RenderWorkflow::addAttachmentResolveOutput() : resource is not an attachment");
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
  if (resIt == end(resources))
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
  {
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
    // resource may only have one transition with output type
  }
  CHECK_LOG_THROW(resType->metaType != RenderWorkflowResourceType::Attachment, "RenderWorkflow::addAttachmentDepthOutput() : resource is not an attachment");
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttAttachmentDepthOutput, layout, loadOp));
  valid = false;
}

void RenderWorkflow::addBufferInput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkPipelineStageFlagBits pipelineStage, VkAccessFlagBits accessFlags, const BufferSubresourceRange& bufferSubresourceRange)
{
  auto operation = getRenderOperation(opName);
  auto resType   = getResourceType(resourceType);
  auto resIt     = resources.find(resourceName);
  if (resIt == end(resources))
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
  CHECK_LOG_THROW(resType->metaType != RenderWorkflowResourceType::Buffer, "RenderWorkflow::addBufferInput() : resource is not a buffer");
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttBufferInput, pipelineStage, accessFlags, bufferSubresourceRange));
  valid = false;
}

void RenderWorkflow::addBufferOutput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkPipelineStageFlagBits pipelineStage, VkAccessFlagBits accessFlags, const BufferSubresourceRange& bufferSubresourceRange)
{
  auto operation = getRenderOperation(opName);
  auto resType   = getResourceType(resourceType);
  auto resIt     = resources.find(resourceName);
  if (resIt == end(resources))
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
  {
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
    // resource may only have one transition with output type
  }
  CHECK_LOG_THROW(resType->metaType != RenderWorkflowResourceType::Buffer, "RenderWorkflow::addBufferOutput() : resource is not a buffer");
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttBufferOutput, pipelineStage, accessFlags, bufferSubresourceRange));
  valid = false;
}

void RenderWorkflow::addImageInput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkImageLayout layout, const ImageSubresourceRange& imageSubresourceRange)
{
  auto operation = getRenderOperation(opName);
  auto resType = getResourceType(resourceType);
  auto resIt = resources.find(resourceName);
  if (resIt == end(resources))
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
  CHECK_LOG_THROW(resType->metaType != RenderWorkflowResourceType::Image && resType->metaType != RenderWorkflowResourceType::Attachment, "RenderWorkflow::addImageInput() : resource is not an image nor attachment");
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttImageInput, layout, loadOpLoad(), imageSubresourceRange));
  valid = false;
}

void RenderWorkflow::addImageOutput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkImageLayout layout, const LoadOp& loadOp, const ImageSubresourceRange& imageSubresourceRange)
{
  auto operation = getRenderOperation(opName);
  auto resType = getResourceType(resourceType);
  auto resIt = resources.find(resourceName);
  if (resIt == end(resources))
    resIt = resources.insert({ resourceName, std::make_shared<WorkflowResource>(resourceName, resType) }).first;
  else
  {
    CHECK_LOG_THROW(resType != resIt->second->resourceType, "RenderWorkflow : ambiguous type of the input");
    // resource may only have one transition with output type
  }
  CHECK_LOG_THROW(resType->metaType != RenderWorkflowResourceType::Image && resType->metaType != RenderWorkflowResourceType::Attachment, "RenderWorkflow::addImageOutput() : resource is not an image nor attachment");
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttImageOutput, layout, loadOp, imageSubresourceRange));
  valid = false;
}

std::shared_ptr<WorkflowResource> RenderWorkflow::getResource(const std::string& resourceName) const
{
  auto it = resources.find(resourceName);
  CHECK_LOG_THROW(it == end(resources), "RenderWorkflow : there is no resource with name " + resourceName);
  return it->second;
}

std::vector<std::string> RenderWorkflow::getResourceNames() const
{
  std::vector<std::string> results;
  for (auto& op : resources)
    results.push_back(op.first);
  return results;
}


void RenderWorkflow::associateMemoryObject(const std::string& name, std::shared_ptr<MemoryObject> memoryObject)
{
  auto resIt = resources.find(name);
  CHECK_LOG_THROW(resIt == end(resources), "RenderWorkflow : cannot associate memory object to nonexisting resource");
  CHECK_LOG_THROW(resIt->second->resourceType->metaType == RenderWorkflowResourceType::Attachment, "Cannot associate memory object with attachment");
  switch (memoryObject->getType())
  {
  case MemoryObject::moBuffer:
    CHECK_LOG_THROW(resIt->second->resourceType->metaType != RenderWorkflowResourceType::Buffer, "RenderWorkflow : cannot associate memory buffer and resource " << resIt->second->name);
    break;
  case MemoryObject::moImage:
    CHECK_LOG_THROW(resIt->second->resourceType->metaType != RenderWorkflowResourceType::Image, "RenderWorkflow : cannot associate memory image and resource " << resIt->second->name);
    break;
  }
  associatedMemoryObjects.insert({ name, memoryObject });
  valid = false;
}

std::shared_ptr<MemoryObject> RenderWorkflow::getAssociatedMemoryObject(const std::string& name) const
{
  auto resIt = associatedMemoryObjects.find(name);
  if (resIt == end(associatedMemoryObjects))
    return std::shared_ptr<MemoryObject>();
  return resIt->second;
}

std::set<std::shared_ptr<RenderOperation>> RenderWorkflow::getInitialOperations() const
{
  // operation is initial when :
  // - there are no input resources, or
  // - all input resources are not generated by any operation

  std::set<std::shared_ptr<RenderOperation>> initialOperations;
  auto operationsNames = getRenderOperationNames();
  for (auto& opName : operationsNames)
  {
    bool isInitial     = true;
    auto inTransitions = getOperationIO(opName, rttAllInputs);
    for (auto& inTransition : inTransitions)
    {
      auto outTransitions = getResourceIO(inTransition->resource->name, rttAllOutputs);
      if (!outTransitions.empty())
      {
        isInitial = false;
        break;
      }
    }
    if (isInitial)
      initialOperations.insert(renderOperations.at(opName));
  }
  return initialOperations;
}

std::set<std::shared_ptr<RenderOperation>> RenderWorkflow::getFinalOperations() const
{
  // operation is final when :
  // - there are no output resources, or
  // - all output resources are not sent to any operation

  std::set<std::shared_ptr<RenderOperation>> finalOperations;
  auto operationsNames = getRenderOperationNames();
  for (auto& opName : operationsNames)
  {
    bool isFinal = true;
    auto outTransitions = getOperationIO(opName, rttAllOutputs);
    for (auto& outTransition : outTransitions)
    {
      auto inTransitions = getResourceIO(outTransition->resource->name, rttAllInputs);
      if (!inTransitions.empty())
      {
        isFinal = false;
        break;
      }
    }
    if (isFinal)
      finalOperations.insert(renderOperations.at(opName));
  }
  return finalOperations;
}

std::set<std::shared_ptr<RenderOperation>> RenderWorkflow::getPreviousOperations(const std::string& opName) const
{
  auto opTransitions = getOperationIO(opName, rttAllInputs);

  std::set<std::shared_ptr<RenderOperation>> previousOperations;
  for (auto& opTransition : opTransitions)
  {
    // operation is final if all of its ouputs are inputs for final operations
    auto resTransitions = getResourceIO(opTransition->resource->name, rttAllOutputs);
    for (auto& resTransition : resTransitions)
      previousOperations.insert(resTransition->operation);
  }
  return previousOperations;
}

std::set<std::shared_ptr<RenderOperation>> RenderWorkflow::getNextOperations(const std::string& opName) const
{
  auto outTransitions = getOperationIO(opName, rttAllOutputs);

  std::set<std::shared_ptr<RenderOperation>> nextOperations;
  for (auto& opTransition : outTransitions)
  {
    // operation is final if all of its ouputs are inputs for final operations
    auto resTransitions = getResourceIO(opTransition->resource->name, rttAllInputs);
    for (auto& resTransition : resTransitions)
      nextOperations.insert(resTransition->operation);
  }
  return nextOperations;
}

std::vector<std::shared_ptr<ResourceTransition>> RenderWorkflow::getOperationIO(const std::string& opName, ResourceTransitionTypeFlags transitionTypes) const
{
  auto operation = getRenderOperation(opName);
  std::vector<std::shared_ptr<ResourceTransition>> results;
  std::copy_if(begin(transitions), end(transitions), std::back_inserter(results),
    [operation, transitionTypes](std::shared_ptr<ResourceTransition> c)->bool{ return c->operation == operation && ( c->transitionType & transitionTypes) ; });
  return results;
}

std::vector<std::shared_ptr<ResourceTransition>> RenderWorkflow::getResourceIO(const std::string& resourceName, ResourceTransitionTypeFlags transitionTypes) const
{
  auto resource = getResource(resourceName);
  std::vector<std::shared_ptr<ResourceTransition>> results;
  std::copy_if(begin(transitions), end(transitions), std::back_inserter(results),
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
  for (auto& operationName : operationNames)
  {
    auto operation = workflow.getRenderOperation(operationName);
    if (operation->operationType != RenderOperation::Graphics)
    {
      attachmentTag.insert({ operationName, currentTag++ });
      continue;
    }
    AttachmentSize attachmentSize = operation->attachmentSize;
    int tagFound = -1;
    for (auto& tit : tags)
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
    for (auto& operationName : operationNames)
    {
      auto operation = workflow.getRenderOperation(operationName);
      auto nextOperations = workflow.getNextOperations(operationName);
      if (nextOperations.empty())
        newDoneOperations.insert(operation);
    }
  }
  else
  {
    for (auto& operationName : operationNames)
    {
      auto operation = workflow.getRenderOperation(operationName);
      if (doneOperations.find(operation) != end(doneOperations))
        continue;
      auto nextOperations = workflow.getNextOperations(operationName);
      bool final = true;
      // check if ALL outputs point at operations in doneOperations
      for (auto& nextOp : nextOperations)
      {
        if (doneOperations.find(nextOp) == end(doneOperations))
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
  auto minit = std::min_element(begin(cost), end(cost));
  auto i = std::distance(begin(cost), minit);
  return results[i];
}

std::shared_ptr<RenderWorkflowSequences> SingleQueueWorkflowCompiler::compile(RenderWorkflow& workflow)
{
  // verify operations
  verifyOperations(workflow);

  // calculate partial ordering
  std::vector<std::shared_ptr<RenderOperation>> partialOrdering = calculatePartialOrdering(workflow);

  // Tags are used to prefer graphics operations with the same tag value to be performed one after another ( subpass grouping ).
  // Look at calculateWorkflowCost()
  // - each compute operation gets its own tag
  // - all graphics operations with the same attachment size get the same tag
  // - two graphics operations with different attachment size get different tag
  costCalculator.tagOperationByAttachmentType(workflow);

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

  // collect information about resources
  std::map<std::string, std::string>             resourceAlias;
  std::vector<FrameBufferImageDefinition>        frameBufferDefinitions;
  std::unordered_map<std::string, uint32_t>      attachmentIndex;
  collectResources(workflow, operationSequences, resourceAlias, frameBufferDefinitions, attachmentIndex);


  // construct render command sequences ( render passes, compute passes )
  std::vector<std::vector<std::shared_ptr<RenderCommand>>> commands;
  for( auto& operationSequence : operationSequences )
  {
    std::vector<std::shared_ptr<RenderCommand>> commandSequence = createCommandSequence(operationSequence, frameBufferDefinitions, attachmentIndex );
    commands.push_back(commandSequence);
  }

  // calculate STORE_OP_STORE and preserve attachments
  finalizeRenderPasses(workflow, commands, partialOrdering, frameBufferDefinitions, attachmentIndex);

  // create initial layouts
  std::vector<VkImageLayout> initialImageLayouts = calculateInitialLayouts(workflow, frameBufferDefinitions, attachmentIndex );

  // create pipeline barriers
  createPipelineBarriers(workflow, commands);

  // find render pass that writes to surface
  uint32_t presentationQueueIndex;
  std::shared_ptr<RenderPass> outputRenderPass = findOutputRenderPass(workflow, commands, presentationQueueIndex);;

  // create frame buffer
  std::shared_ptr<FrameBuffer> fb = std::make_shared<FrameBuffer>(frameBufferDefinitions, outputRenderPass, workflow.frameBufferAllocator);

  return std::make_shared<RenderWorkflowSequences>(workflow.getQueueTraits(), commands, fb, initialImageLayouts, outputRenderPass, presentationQueueIndex);
}

void SingleQueueWorkflowCompiler::verifyOperations(const RenderWorkflow& workflow)
{
  std::ostringstream os;
  // check if all attachments have the same size as defined in operation
  auto operationNames = workflow.getRenderOperationNames();
  for (auto& operationName : operationNames)
  {
    auto operation     = workflow.getRenderOperation(operationName);
    auto opTransitions = workflow.getOperationIO(operationName, rttAllAttachments);
    for (auto& transition : opTransitions)
    {
      if(transition->resource->resourceType->attachment.attachmentSize != operation->attachmentSize )
        os << "Error: Operation <" << operationName << "> : attachment "<< transition->resource->name <<" has wrong size" << std::endl;
    }
  }
  // check if all resources have at most one output that generates them
  auto resourceNames = workflow.getResourceNames();
  for (auto& resourceName : resourceNames)
  {
    auto resource = workflow.getResource(resourceName);
    auto opTransitions = workflow.getResourceIO(resourceName, rttAllOutputs);
    if(opTransitions.size()>1)
      os << "Error: Resource <" << resourceName << "> : resource must have at most one output that generates it" << std::endl;
  }

  // FIXME : check for loops in workflow

  // if there are some errors - throw exception
  std::string results;
  results = os.str();
  CHECK_LOG_THROW(!results.empty(), "Errors in workflow definition :\n" + results);
}

std::vector<std::shared_ptr<RenderOperation>> SingleQueueWorkflowCompiler::calculatePartialOrdering(const RenderWorkflow& workflow)
{
  std::vector<std::shared_ptr<RenderOperation>> partialOrdering;

  // resources that have only input transitions are sent by CPU and are not modified during workflow execution
  std::set<std::shared_ptr<WorkflowResource>> existingResources;
  auto resourceNames = workflow.getResourceNames();
  for (auto& resourceName : resourceNames)
  {
    auto outTransitions = workflow.getResourceIO(resourceName, rttAllOutputs);
    if (outTransitions.empty())
      existingResources.insert(workflow.getResource(resourceName));
  }
  // initial operations are operations without inputs, or operations which inputs should be on existing resources list
  std::set<std::shared_ptr<RenderOperation>> nextOperations = workflow.getInitialOperations();
  std::set<std::shared_ptr<RenderOperation>> doneOperations;
  std::set<std::shared_ptr<WorkflowResource>> doneResources;

  while (!nextOperations.empty())
  {
    std::set<std::shared_ptr<RenderOperation>> nextOperations2;
    for (auto& operation : nextOperations)
    {
      // if operation has no inputs, or all inputs are on existingResources then operation may be added to partial ordering
      auto inTransitions = workflow.getOperationIO(operation->name, rttAllInputs);
      uint32_t notExistingInputs = std::count_if(begin(inTransitions), end(inTransitions), 
        [&existingResources](std::shared_ptr<ResourceTransition> transition) { return existingResources.find(transition->resource) == end(existingResources); });
      if (notExistingInputs == 0)
      {
        // operation is performed - add it to partial ordering
        partialOrdering.push_back(operation);
        doneOperations.insert(operation);
        // mark output resources as existing
        auto outTransitions = workflow.getOperationIO(operation->name, rttAllOutputs);
        for (auto& outTransition : outTransitions)
          existingResources.insert(outTransition->resource);
        // add next operations to nextOperations2
        auto follow = workflow.getNextOperations(operation->name);
        std::copy(begin(follow), end(follow), std::inserter(nextOperations2, end(nextOperations2)));
      }
    }
    nextOperations.clear();
    std::copy_if(begin(nextOperations2), end(nextOperations2), std::inserter(nextOperations, end(nextOperations)), [&doneOperations](std::shared_ptr<RenderOperation> op) { return doneOperations.find(op) == end(doneOperations); });
  }
  return partialOrdering;
}

std::vector<std::string> recursiveLongestPath(const std::vector<std::pair<std::string, std::string>>& resourcePairs, const std::set<std::string>& doneVertices = std::set<std::string>())
{
  std::set<std::string> vertices;
  if (doneVertices.empty())
  {
    for (auto& rp : resourcePairs)
      vertices.insert(rp.first);
    for (auto& rp : resourcePairs)
      vertices.erase(rp.second);
  }
  else
  {
    for (auto& rp : resourcePairs)
    {
      if (doneVertices.find(rp.first) == end(doneVertices))
        continue;
      if (doneVertices.find(rp.second) != end(doneVertices))
        continue;
      vertices.insert(rp.second);
    }
  }
  if (vertices.empty())
    return std::vector<std::string>();


  std::vector<std::vector<std::string>> results;
  for (const auto& x : vertices)
  {
    auto a = doneVertices;
    a.insert(x);
    auto xx = recursiveLongestPath(resourcePairs, a);
    xx.push_back(x);
    results.push_back(xx);
  }
  uint32_t maxSize = 0;
  int maxElt = -1;
  for (uint32_t i = 0; i<results.size(); ++i)
  {
    if (results[i].size() > maxSize)
    {
      maxSize = results[i].size();
      maxElt = i;
    }
  }
  return results[maxElt];
}

void SingleQueueWorkflowCompiler::collectResources(const RenderWorkflow& workflow, const std::vector<std::vector<std::shared_ptr<RenderOperation>>>& operationSequences, std::map<std::string, std::string>& resourceAlias, std::vector<FrameBufferImageDefinition>& frameBufferDefinitions, std::unordered_map<std::string, uint32_t>& attachmentIndex)
{
  resourceAlias.clear();
  frameBufferDefinitions.clear();
  attachmentIndex.clear();

  std::map<std::string, std::pair<uint32_t,uint32_t>> operationIndex;
  for (uint32_t i = 0; i < operationSequences.size(); ++i)
    for (uint32_t j = 0; j < operationSequences[i].size(); ++j)
      operationIndex.insert({ operationSequences[i][j]->name, {i,j} });

  // collect resources that may be used again
  // Resource may be used again when :
  // - all of its inputs are used in the same queue
  // - it is an attachment or has no associated resource
  // - it is not a swapchain image
  // aliasingPossible stores queue index in which resource is created and queue index in which resource is consumed ( -1 if there's no generation/consumption )
  // also it stores operation indices
  std::map<std::string,std::tuple<int,int,int,int,int>> aliasingPossible;
  auto resourceNames = workflow.getResourceNames();
  for (auto& resourceName : resourceNames)
  {
    resourceAlias.insert({ resourceName , resourceName });
    auto resource = workflow.getResource(resourceName);
    if (resource->resourceType->metaType != RenderWorkflowResourceType::Attachment && workflow.getAssociatedMemoryObject(resourceName) != nullptr)
      continue;
    if (resource->resourceType->attachment.attachmentType == atSurface)
      continue;
    auto inputTransitions = workflow.getResourceIO(resourceName, rttAllInputs);
    std::set<uint32_t> consumedInQueue;
    std::set<int> inOpIndices;
    for (auto& inputTransition : inputTransitions)
    {
      consumedInQueue.insert(operationIndex.at(inputTransition->operation->name).first);
      inOpIndices.insert(operationIndex.at(inputTransition->operation->name).second);
    }
    if (consumedInQueue.size() > 1)
      continue;

    int inQueueIndex = consumedInQueue.empty() ? -1 : *begin(consumedInQueue);
    int inOpFirst    = inOpIndices.empty() ? -1 : *begin(inOpIndices);
    int inOpLast     = inOpIndices.empty() ? -1 : *rbegin(inOpIndices);

    auto outTransitions = workflow.getResourceIO(resourceName, rttAllOutputs);
    int outQueueIndex = outTransitions.empty() ? -1 : operationIndex.at(outTransitions[0]->operation->name).first;
    int outOpIndex    = outTransitions.empty() ? -1 : operationIndex.at(outTransitions[0]->operation->name).second;

    aliasingPossible.insert( { resourceName,std::make_tuple( outQueueIndex, outOpIndex, inQueueIndex, inOpFirst, inOpLast ) } );
  }

  // OK, so we have a group of resources where resource reuse is possible. First - we build pairs of resources that may be reused
  // Remark : algorithm complexity is n^2
  std::vector<std::pair<std::string, std::string>> resourcePairs;
  for (auto it0 = begin(aliasingPossible); it0 != end(aliasingPossible); ++it0)
  {
    auto resource0 = workflow.getResource(it0->first);
    // cannot overwrite a resource marked as persistent
    if (resource0->resourceType->persistent)
      continue;
    int r0oq, r0op, r0iq, r0ip0, r0ip1;
    std::tie(r0oq, r0op, r0iq, r0ip0, r0ip1) = it0->second;
    // let's skip some empty resources ( this shouldnt happen, but... )
    if (r0oq == -1 && r0iq == -1)
      continue;
    // Question : what to do with resources that are generated but are not persistent and are not used later (r0iq==-1) ?
    // For now we will assume that this resource may be used later in the same queue
    if (r0iq == -1)
    {
      r0iq  = r0oq;
      r0ip0 = r0ip1 = r0op;
    }
    for (auto it1 = begin(aliasingPossible); it1 != end(aliasingPossible); ++it1)
    {
      // cannot merge resource with itself
      if (it0 == it1) continue;
      auto resource1 = workflow.getResource(it1->first);
      if (!resource0->resourceType->isEqual(*(resource1->resourceType)))
        continue;
      int r1oq, r1op, r1iq, r1ip0, r1ip1;
      std::tie(r1oq, r1op, r1iq, r1ip0, r1ip1) = it1->second;
      // if resource r1 is not generated ( r1oq==-1 ) then it is sent from outside the workflow and we should not try to merge r0->r1
      if (r1oq == -1)
        continue;

      // skip it if last use of r0 is in other queue than creation of r1
      if (r0iq != r1oq)
        continue;
      // if last consumption of r0 happens after generation of r1
      if (r0ip1 >= r1op) continue;

      resourcePairs.push_back({ it0->first , it1->first });
    }
  }
  // we can find reuse schema having graph from resource pairs that can be reused. This algorithm should minimize the number of output elements
  auto resourcePairsBkp = resourcePairs;
  while (!resourcePairs.empty())
  {
    auto longestPath = recursiveLongestPath(resourcePairs);

    std::vector<std::pair<std::string, std::string>> rp;
    std::copy_if(begin(resourcePairs), end(resourcePairs), std::back_inserter(rp), 
      [&longestPath](const std::pair<std::string, std::string>& thisPair) { return std::find(begin(longestPath), end(longestPath), thisPair.first) == end(longestPath) && std::find(begin(longestPath), end(longestPath), thisPair.second) == end(longestPath); });
    resourcePairs = rp;

    if (longestPath.size() > 1)
    {
      auto target = longestPath.back();
      longestPath.pop_back();
      for (auto& p : longestPath)
        resourceAlias[p] = target;
    }
    else break;
  }

  // build attachmentIndex and frameBufferDefinitions
  for (auto& resourceName : resourceNames)
  {
    auto resource     = workflow.getResource(resourceName);
    auto resourceType = resource->resourceType;

    // do not create resources that will be aliased
    if (resourceAlias[resourceName] == resourceName)
    {
      attachmentIndex.insert({ resourceName, static_cast<uint32_t>(frameBufferDefinitions.size()) });

      if (resourceType->metaType == RenderWorkflowResourceType::Attachment)
      {
        frameBufferDefinitions.push_back(FrameBufferImageDefinition(
          resourceType->attachment.attachmentType,
          resourceType->attachment.format,
          resourceType->attachment.imageUsage,
          getAspectMask(resourceType->attachment.attachmentType),
          resourceType->attachment.samples,
          resourceName,
          resourceType->attachment.attachmentSize,
          resourceType->attachment.swizzles
        ));
      }
    }
  }
  for (auto& alias : resourceAlias)
    if (alias.first != alias.second)
      attachmentIndex.insert({ alias.first, attachmentIndex[alias.second] });
}

std::vector<std::shared_ptr<RenderCommand>> SingleQueueWorkflowCompiler::createCommandSequence(const std::vector<std::shared_ptr<RenderOperation>>& operationSequence, std::vector<FrameBufferImageDefinition>& frameBufferDefinitions, const std::unordered_map<std::string, uint32_t>& attachmentIndex)
{
  std::vector<std::shared_ptr<RenderCommand>> results;

  int lastTag = -1;
  std::shared_ptr<RenderPass> lastRenderPass;

  std::vector<VkImageLayout> lastLayout(frameBufferDefinitions.size());
  std::fill(begin(lastLayout), end(lastLayout), VK_IMAGE_LAYOUT_UNDEFINED);
  
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
        lastRenderPass = std::make_shared<RenderPass>();
        lastRenderPass->initializeAttachments(frameBufferDefinitions, attachmentIndex, lastLayout);
      }

      std::shared_ptr<RenderSubPass> renderSubPass = std::make_shared<RenderSubPass>();
      renderSubPass->operation = operation;
      renderSubPass->buildSubPassDefinition(attachmentIndex);

      lastRenderPass->addSubPass(renderSubPass);
      lastRenderPass->updateAttachments(renderSubPass, frameBufferDefinitions, attachmentIndex, lastLayout);

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
  return results;
}

std::vector<VkImageLayout> SingleQueueWorkflowCompiler::calculateInitialLayouts(const RenderWorkflow& workflow, std::vector<FrameBufferImageDefinition>& frameBufferDefinitions, const std::unordered_map<std::string, uint32_t>& attachmentIndex)
{
  std::vector<VkImageLayout> results;
  results.resize(frameBufferDefinitions.size(), VK_IMAGE_LAYOUT_UNDEFINED);

  std::set<std::shared_ptr<RenderOperation>> nextOperations = workflow.getInitialOperations();
  std::set<std::shared_ptr<RenderOperation>> doneOperations;
  while (!nextOperations.empty())
  {
    std::set<std::shared_ptr<RenderOperation>> followingOperations;
    for (auto& operation : nextOperations)
    {
      auto opTransitions = workflow.getOperationIO(operation->name, rttAllAttachmentOutputs);
      for (auto& transition : opTransitions)
      {
        auto attIndex = attachmentIndex.at(transition->resource->name);
        if (results[attIndex] == VK_IMAGE_LAYOUT_UNDEFINED)
          results[attIndex] = transition->attachment.layout;
      }
      doneOperations.insert(operation);
      auto follow = workflow.getNextOperations(operation->name);
      std::copy(begin(follow), end(follow), std::inserter(followingOperations, end(followingOperations)));
    }
    nextOperations.clear();
    std::copy_if(begin(followingOperations), end(followingOperations), std::inserter(nextOperations, end(nextOperations)), [&doneOperations](std::shared_ptr<RenderOperation> op) { return doneOperations.find(op) == end(doneOperations); });
  }
  return results;
}

void SingleQueueWorkflowCompiler::finalizeRenderPasses(const RenderWorkflow& workflow, const std::vector<std::vector<std::shared_ptr<RenderCommand>>>& commands, const std::vector<std::shared_ptr<RenderOperation>>& partialOrdering, std::vector<FrameBufferImageDefinition>& frameBufferDefinitions, const std::unordered_map<std::string, uint32_t>& attachmentIndex)
{
  for (auto& commandSequence : commands)
  {
    for( auto it = begin(commandSequence); it != end(commandSequence); )
    {
      auto rsp = (*it)->asRenderSubPass();
      if (rsp == nullptr)
      {
        ++it;
        continue;
      }
      // find the end of render pass
      auto renderPass = rsp->renderPass;
      auto eit = it;
      std::set<std::shared_ptr<RenderOperation>> renderPassOperations;
      for ( ; eit != end(commandSequence); ++eit)
      {
        auto rsp = (*eit)->asRenderSubPass();
        if (rsp == nullptr)
          break;
        if (rsp->renderPass != renderPass)
          break;
        renderPassOperations.insert(rsp->operation);
      }

      for (auto xit = it; xit != eit; ++xit)
      {
        auto renderSubpass   = (*xit)->asRenderSubPass();
        auto inTransitions   = workflow.getOperationIO(renderSubpass->operation->name, rttAllAttachmentInputs);
        auto outTransitions  = workflow.getOperationIO(renderSubpass->operation->name, rttAllAttachmentOutputs);
        auto thisOperation   = std::find(begin(partialOrdering), end(partialOrdering), renderSubpass->operation);

        bool lastSubpass = ( (xit+1) == eit );

        // check which resources are used in a subpass
        std::vector<char> usedNow;
        usedNow.resize(frameBufferDefinitions.size(), false);
        for (auto& inTransition : inTransitions)
        {
          auto attIndex = attachmentIndex.at(inTransition->resource->name);
          usedNow[attIndex] = true;
        }
        for (auto& outTransition : outTransitions)
        {
          auto attIndex = attachmentIndex.at(outTransition->resource->name);
          usedNow[attIndex] = true;
        }
        // for each unused resource : it must be preserved when
        // - it is persistent
        // - it is swapchain image
        // - it was used before
        // - it is used later in a subpass or outside 
        for (unsigned int i = 0; i < usedNow.size(); ++i)
        {
          auto resource = workflow.getResource(frameBufferDefinitions[i].name);
          auto resOutTransitions = workflow.getResourceIO(frameBufferDefinitions[i].name, rttAllOutputs);
          auto resInTransitions  = workflow.getResourceIO(frameBufferDefinitions[i].name, rttAllInputs);

          bool usedLater = false;
          for (auto& transition : resInTransitions)
          {
            if (std::find(thisOperation + 1, end(partialOrdering), transition->operation) != end(partialOrdering))
            {
              usedLater = true;
              break;
            }
          }
          bool usedBefore = false;
          for (auto& transition : resOutTransitions)
          {
            if (std::find(begin(partialOrdering), thisOperation, transition->operation) != end(partialOrdering))
            {
              usedBefore = true;
              break;
            }
          }
          bool isSurfaceOrPersistent = resource->resourceType->attachment.attachmentType == atSurface || resource->resourceType->persistent;
          bool preserve = usedBefore && !usedNow[i] && ( usedLater || isSurfaceOrPersistent);
          bool save = lastSubpass && (usedLater || isSurfaceOrPersistent);

          if(preserve)
            renderSubpass->definition.preserveAttachments.push_back(i);
          if (save)
          {
            AttachmentType at = resource->resourceType->attachment.attachmentType;
            bool colorDepthAttachment = (at == atSurface) || (at == atColor) || (at == atDepth) || (at == atDepthStencil);
            bool stencilAttachment = (at == atDepthStencil) || (at == atStencil);

            if (colorDepthAttachment) renderPass->attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            if (stencilAttachment)    renderPass->attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
          }
        }
      }
      it = eit;
    }
  }
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
  for (auto& resourceName : resourceNames)
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
    auto pos = std::partition(begin(consumingTransitions), end(consumingTransitions), [&queueNumber, &generatingQueueNumber](std::shared_ptr<ResourceTransition> lhs) 
    {
      return queueNumber[lhs->operation->name] == generatingQueueNumber;
    });
    // sort transitions from the same queue according to order of operations
    std::sort(begin(consumingTransitions), pos, [&operationNumber](std::shared_ptr<ResourceTransition> lhs, std::shared_ptr<ResourceTransition> rhs)
    {
      return operationNumber[lhs->operation->name] < operationNumber[rhs->operation->name];
    });
    // sort transitions from other queues
    std::sort(pos, end(consumingTransitions), [&queueNumber, &operationNumber](std::shared_ptr<ResourceTransition> lhs, std::shared_ptr<ResourceTransition> rhs)
    {
      if (queueNumber[lhs->operation->name] == queueNumber[rhs->operation->name])
        return operationNumber[lhs->operation->name] < operationNumber[rhs->operation->name];
      return queueNumber[lhs->operation->name] < queueNumber[rhs->operation->name];
    });

    // for now we will create a barrier/subpass dependency for each transition. It should be later optimized ( some barriers are not necessary )
    for (auto& consumingTransition : consumingTransitions)
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

    auto dep = std::find_if(begin(consumingSubpass->renderPass->dependencies), end(consumingSubpass->renderPass->dependencies), 
      [srcSubpassIndex, dstSubpassIndex](const SubpassDependencyDefinition& sd) -> bool { return sd.srcSubpass == srcSubpassIndex && sd.dstSubpass == dstSubpassIndex; });
    if (dep == end(consumingSubpass->renderPass->dependencies))
      dep = consumingSubpass->renderPass->dependencies.insert(end(consumingSubpass->renderPass->dependencies), SubpassDependencyDefinition(srcSubpassIndex, dstSubpassIndex, 0, 0, 0, 0, 0));
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

    auto dep = std::find_if(begin(generatingSubpass->renderPass->dependencies), end(generatingSubpass->renderPass->dependencies),
      [srcSubpassIndex, dstSubpassIndex](const SubpassDependencyDefinition& sd) -> bool { return sd.srcSubpass == srcSubpassIndex && sd.dstSubpass == dstSubpassIndex; });
    if (dep == end(generatingSubpass->renderPass->dependencies))
      dep = generatingSubpass->renderPass->dependencies.insert(end(generatingSubpass->renderPass->dependencies), SubpassDependencyDefinition(srcSubpassIndex, dstSubpassIndex, 0, 0, 0, 0, 0));
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

  // If there's no associated memory object then there can be no pipeline barrier
  // Some inputs/outputs may be added without memory objects just to enforce proper order of operations
  auto memoryObject = workflow->getAssociatedMemoryObject(generatingTransition->resource->name);
  if (memoryObject.get() == nullptr)
    return;

  uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  if (generatingQueueIndex != consumingQueueIndex)
  {
    // FIXME - find family indices for both queues
  }

  VkPipelineStageFlags srcStageMask = 0,  dstStageMask = 0;
  VkAccessFlags        srcAccessMask = 0, dstAccessMask = 0;
  getPipelineStageMasks(generatingTransition, consumingTransition, srcStageMask, dstStageMask);
  getAccessMasks(generatingTransition, consumingTransition, srcAccessMask, dstAccessMask);

  VkDependencyFlags dependencyFlags = 0; // FIXME

  MemoryObjectBarrierGroup rbg(srcStageMask, dstStageMask, dependencyFlags);
  auto rbgit = consumingCommand->barriersBeforeOp.find(rbg);
  if (rbgit == end(consumingCommand->barriersBeforeOp))
    rbgit = consumingCommand->barriersBeforeOp.insert({ rbg, std::vector<MemoryObjectBarrier>() }).first;
  switch (generatingTransition->resource->resourceType->metaType)
  {
  case RenderWorkflowResourceType::Buffer:
  {
    auto bufferRange = generatingTransition->buffer.bufferSubresourceRange;
    rbgit->second.push_back(MemoryObjectBarrier(srcAccessMask, dstAccessMask, srcQueueFamilyIndex, dstQueueFamilyIndex, memoryObject, bufferRange));
    break;
  }
  case RenderWorkflowResourceType::Image:
  {
    VkImageLayout oldLayout = generatingTransition->attachment.layout;
    VkImageLayout newLayout = consumingTransition->attachment.layout;
    auto imageRange         = generatingTransition->image.imageSubresourceRange;
    rbgit->second.push_back(MemoryObjectBarrier(srcAccessMask, dstAccessMask, srcQueueFamilyIndex, dstQueueFamilyIndex, memoryObject, oldLayout, newLayout, imageRange));
    break;
  }
  }
}

std::shared_ptr<RenderPass> SingleQueueWorkflowCompiler::findOutputRenderPass(const RenderWorkflow& workflow, const std::vector<std::vector<std::shared_ptr<RenderCommand>>>& commands, uint32_t& presentationQueueIndex)
{
  std::shared_ptr<RenderPass> outputRenderPass;
  presentationQueueIndex = 0;
  for (auto& commandSequence : commands)
  {
    for (int i = commandSequence.size() - 1; i >= 0; --i)
    {
      if (commandSequence[i]->commandType != RenderCommand::ctRenderSubPass)
        continue;
      bool found = false;
      auto transitions = workflow.getOperationIO(commandSequence[i]->operation->name, rttAttachmentOutput | rttAttachmentResolveOutput);
      for (auto& transition : transitions)
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
  return outputRenderPass;
}
