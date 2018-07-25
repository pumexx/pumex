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

RenderOperation::RenderOperation(const std::string& n, RenderOperation::Type t, uint32_t mvm, AttachmentSize at )
  : name{ n }, operationType{ t }, multiViewMask{ mvm }, attachmentSize{ at }
{
}

RenderOperation::~RenderOperation()
{
}

void RenderOperation::setRenderWorkflow ( std::shared_ptr<RenderWorkflow> rw )
{
  renderWorkflow = rw;
}

void RenderOperation::setRenderOperationNode(std::shared_ptr<Node> n)
{
  node = n;
}

std::shared_ptr<Node> RenderOperation::getRenderOperationNode()
{
  return node;
}

ResourceTransition::ResourceTransition(std::shared_ptr<RenderOperation> op, std::shared_ptr<WorkflowResource> res, ResourceTransitionType tt, VkImageLayout l, const LoadOp& ld)
  : operation{ op }, resource{ res }, transitionType{ tt }, layout{ l }, load{ ld }, resolveResource{}, imageSubresourceRange{}, pipelineStage{ VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT }, accessFlags{ VK_ACCESS_UNIFORM_READ_BIT }, bufferSubresourceRange{}
{
}

ResourceTransition::ResourceTransition(std::shared_ptr<RenderOperation> op, std::shared_ptr<WorkflowResource> res, ResourceTransitionType tt, VkPipelineStageFlags ps, VkAccessFlags af, const BufferSubresourceRange& bsr)
  : operation{ op }, resource{ res }, transitionType{ tt }, layout{ VK_IMAGE_LAYOUT_UNDEFINED }, load{}, resolveResource{}, imageSubresourceRange{}, pipelineStage{ ps }, accessFlags{ af }, bufferSubresourceRange{ bsr }
{
  
}

ResourceTransition::ResourceTransition(std::shared_ptr<RenderOperation> op, std::shared_ptr<WorkflowResource> res, ResourceTransitionType tt, VkImageLayout l, const LoadOp& ld, const ImageSubresourceRange& isr)
  : operation{ op }, resource{ res }, transitionType{ tt }, layout{ l }, load{ ld }, resolveResource{}, imageSubresourceRange{ isr }, pipelineStage{ VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT }, accessFlags{ VK_ACCESS_UNIFORM_READ_BIT }, bufferSubresourceRange{}
{
}

ResourceTransition::~ResourceTransition()
{
}

RenderWorkflowResults::RenderWorkflowResults()
{
}

QueueTraits RenderWorkflowResults::getPresentationQueue() const
{
  return queueTraits[presentationQueueIndex];
}

FrameBufferImageDefinition RenderWorkflowResults::getSwapChainImageDefinition() const
{
  for (const auto& frameBuffer : frameBuffers)
  {
    for (uint32_t i = 0; i < frameBuffer->getNumImageDefinitions(); ++i)
    {
      const FrameBufferImageDefinition& fbid = frameBuffer->getImageDefinition(i);
      if (fbid.attachmentType == atSurface)
        return FrameBufferImageDefinition(fbid);
    }
  }
  CHECK_LOG_THROW(true, "There's no framebuffer with swapchain image definition");
  return FrameBufferImageDefinition();
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

void RenderWorkflow::addRenderOperation(const std::string& name, RenderOperation::Type operationType, uint32_t multiViewMask, AttachmentSize attachmentSize )
{
  addRenderOperation(std::make_shared<RenderOperation>(name, operationType, multiViewMask, attachmentSize));
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

void RenderWorkflow::setRenderOperationNode(const std::string& opName, std::shared_ptr<Node> n)
{
  getRenderOperation(opName)->node = n;
  valid = false;
}

std::shared_ptr<Node> RenderWorkflow::getRenderOperationNode(const std::string& opName)
{
  return getRenderOperation(opName)->node;
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
  resourceTransition->resolveResource = resolveIt->second;
  transitions.push_back(resourceTransition);
  valid = false;
}

void RenderWorkflow::addAttachmentDepthInput(const std::string& opName, const std::string& resourceType, const std::string& resourceName, VkImageLayout layout)
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
  transitions.push_back(std::make_shared<ResourceTransition>(operation, resIt->second, rttAttachmentDepthInput, layout, loadOpLoad()));
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

void RenderWorkflow::associateMemoryObject(const std::string& name, std::shared_ptr<MemoryObject> memoryObject, VkImageViewType imageViewType)
{
  auto resIt = resources.find(name);
  CHECK_LOG_THROW(resIt == end(resources), "RenderWorkflow : cannot associate memory object to nonexisting resource");
  CHECK_LOG_THROW(resIt->second->resourceType->metaType == RenderWorkflowResourceType::Attachment, "Cannot associate memory object with attachment. Attachments are created by RenderWorkflow");
  switch (memoryObject->getType())
  {
  case MemoryObject::moBuffer:
  {
    CHECK_LOG_THROW(resIt->second->resourceType->metaType != RenderWorkflowResourceType::Buffer, "RenderWorkflow : cannot associate memory buffer and resource " << resIt->second->name);
    associatedMemoryObjects.insert({ name, memoryObject });
    break;
  }
  case MemoryObject::moImage:
  {
    CHECK_LOG_THROW(resIt->second->resourceType->metaType != RenderWorkflowResourceType::Image, "RenderWorkflow : cannot associate memory image and resource " << resIt->second->name);
    associatedMemoryObjects.insert({ name, memoryObject });
    auto memoryImage = std::dynamic_pointer_cast<MemoryImage>(memoryObject);
    auto imageView = std::make_shared<ImageView>(memoryImage, memoryImage->getFullImageRange(), imageViewType);
    associatedMemoryImageViews.insert({ name, imageView });
    break;
  }
  default:
    break;
  }
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
  workflowResults = compiler->compile(*this);
  valid = true;
  return true;
};

void StandardRenderWorkflowCostCalculator::tagOperationByAttachmentType(const RenderWorkflow& workflow)
{
  std::unordered_map<int, AttachmentSize> tags;
  attachmentTag.clear();
  int currentTag = 0;

  for (auto& operationName : workflow.getRenderOperationNames())
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

std::shared_ptr<RenderWorkflowResults> SingleQueueWorkflowCompiler::compile(RenderWorkflow& workflow)
{
  // verify operations
  verifyOperations(workflow);

  // calculate partial ordering
  std::vector<std::shared_ptr<RenderOperation>> partialOrdering;
  calculatePartialOrdering(workflow, partialOrdering);

  std::map<std::string, uint32_t>         resourceMap;
  std::map<std::string, uint32_t>         operationMap;
  std::vector<std::vector<VkImageLayout>> allLayouts;
  calculateAttachmentLayouts(workflow, partialOrdering, resourceMap, operationMap, allLayouts);

  auto workflowResults         = std::make_shared<RenderWorkflowResults>();
  workflowResults->queueTraits = workflow.getQueueTraits();

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

  // find resources that may be reused
  findAliasedResources(workflow, operationSequences, workflowResults);

  // construct render command sequences ( render passes, compute passes )
  std::vector<std::shared_ptr<RenderPass>> renderPasses;
  std::vector<std::vector<std::shared_ptr<RenderCommand>>> commands;
  for( auto& operationSequence : operationSequences )
  {
    std::vector<std::shared_ptr<RenderCommand>> commandSequence;
    createCommandSequence(operationSequence, commandSequence);
    commands.push_back(commandSequence);
  }
  workflowResults->commands = commands;

  // Build framebuffer for each render pass
  // TODO : specification is not clear what compatible render passes are. Neither are debug layers. One day I will decrease the number of frame buffers
  buildFrameBuffersAndRenderPasses(workflow, partialOrdering, resourceMap, operationMap, allLayouts, workflowResults);// resourceAlias, renderPasses, attachmentImages, attachmentImageViews, imageInitialLayouts, frameBuffers);

  // copy RenderWorkflow associated resources to RenderWorkflowResults registered resources
  for (auto& mit : workflow.getAssociatedMemoryObjects())
  {
    workflowResults->resourceAlias.insert({ mit.first, mit.first });
    switch (mit.second->getType())
    {
    case MemoryObject::moBuffer:
      workflowResults->registeredMemoryBuffers.insert({ mit.first, std::dynamic_pointer_cast<MemoryBuffer>(mit.second) });
      break;
    case MemoryObject::moImage:
      workflowResults->registeredMemoryImages.insert({ mit.first, std::dynamic_pointer_cast<MemoryImage>(mit.second) });
      break;
    default:
      break;
    }
  }
  // also copy image views
  for (const auto& iv : workflow.getAssociatedImageViews())
    workflowResults->registeredImageViews.insert({ iv.first, iv.second });

  // create pipeline barriers
  createPipelineBarriers(workflow, commands, workflowResults);

  return workflowResults;
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
  // check if any attachment is used as input and output in the same operation
  for (auto& operationName : operationNames)
  {
    auto inTransitions  = workflow.getOperationIO(operationName, rttAllInputs);
    auto outTransitions = workflow.getOperationIO(operationName, rttAllOutputs);
    std::set<std::string> outNames;
    for (const auto& outTransition : outTransitions)
      outNames.insert(outTransition->resource->name);
    for (const auto& inTransition : inTransitions)
      if(outNames.find(inTransition->resource->name) != outNames.end())
        os << "Error: Resource <" << inTransition->resource->name << "> is used as input and output in the same operation" << std::endl;
  }

  // TODO : check for loops in a workflow

  // if there are some errors - throw exception
  std::string results;
  results = os.str();
  CHECK_LOG_THROW(!results.empty(), "Errors in workflow definition :\n" + results);
}

void SingleQueueWorkflowCompiler::calculatePartialOrdering(const RenderWorkflow& workflow, std::vector<std::shared_ptr<RenderOperation>>& partialOrdering)
{
  partialOrdering.clear();

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
}

void SingleQueueWorkflowCompiler::calculateAttachmentLayouts(const RenderWorkflow& workflow, const std::vector<std::shared_ptr<RenderOperation>>& partialOrdering, std::map<std::string, uint32_t>& resourceMap, std::map<std::string, uint32_t>& operationMap, std::vector<std::vector<VkImageLayout>>& allLayouts)
{
  resourceMap.clear();
  operationMap.clear();
  allLayouts.clear();

  // collect all images and attachments
  std::vector<VkImageLayout> allAttachmentsAndImages;
  uint32_t resid = 0;
  for (auto& resourceName : workflow.getResourceNames())
  {
    auto resource = workflow.getResource(resourceName);
    if (!resource->resourceType->isImageOrAttachment())
      continue;
    resourceMap[resourceName] = resid++;
    allAttachmentsAndImages.push_back(VK_IMAGE_LAYOUT_UNDEFINED);
  }

  // iterate over partialOrdering collecting all used layouts for images and attachments, first use of an attachment defines initial layout
  auto currentLayouts  = allAttachmentsAndImages;
  uint32_t opid = 0;
  for (const auto& operation : partialOrdering)
  {
    operationMap[operation->name] = opid++;

    for (const auto& transition : workflow.getOperationIO(operation->name, rttAllAttachments | rttImageInput | rttImageOutput))
    {
      uint32_t rid = resourceMap[transition->resource->name];
      currentLayouts[rid] = transition->layout;
    }
    allLayouts.push_back(currentLayouts);
  }
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

void SingleQueueWorkflowCompiler::findAliasedResources(const RenderWorkflow& workflow, const std::vector<std::vector<std::shared_ptr<RenderOperation>>>& operationSequences, std::shared_ptr<RenderWorkflowResults> workflowResults)
{
  workflowResults->resourceAlias.clear();

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
    workflowResults->resourceAlias.insert({ resourceName , resourceName });
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
        workflowResults->resourceAlias[p] = target;
    }
    else break;
  }
}

void SingleQueueWorkflowCompiler::createCommandSequence(const std::vector<std::shared_ptr<RenderOperation>>& operationSequence, std::vector<std::shared_ptr<RenderCommand>>& commands)
{
  commands.clear();

  int                                      lastTag = -1;
  std::shared_ptr<RenderPass>              lastRenderPass;
  
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
      }

      std::shared_ptr<RenderSubPass> renderSubPass = std::make_shared<RenderSubPass>();
      renderSubPass->operation = operation;
      lastRenderPass->addSubPass(renderSubPass);
      if (operation->multiViewMask != 0)
        lastRenderPass->multiViewRenderPass = true;
      commands.push_back(renderSubPass);
      break;
    }
    case RenderOperation::Compute:
    {
      lastRenderPass = nullptr;

      std::shared_ptr<ComputePass> computePass = std::make_shared<ComputePass>();
      computePass->operation = operation;
      commands.push_back(computePass);
      break;
    }
    default:
      break;
    }

    lastTag = tag;
  }
}

void SingleQueueWorkflowCompiler::buildFrameBuffersAndRenderPasses(const RenderWorkflow& workflow, const std::vector<std::shared_ptr<RenderOperation>>& partialOrdering, const std::map<std::string, uint32_t>& resourceMap, const std::map<std::string, uint32_t>& operationMap, const std::vector<std::vector<VkImageLayout>>& allLayouts, std::shared_ptr<RenderWorkflowResults> workflowResults)
{
  // find all render passes and find presentationQueueIndex
  std::vector<std::shared_ptr<RenderPass>> renderPasses;
  workflowResults->presentationQueueIndex = 0;
  for (int j = 0; j<workflowResults->commands.size(); ++j)
  {
    // search for atSurface backwards - it's most probably created at the end of a sequence
    for (uint32_t i = 0; i<workflowResults->commands[j].size(); ++i)
    {
      if (workflowResults->commands[j][i]->commandType != RenderCommand::ctRenderSubPass)
        continue;
      auto subpass = std::dynamic_pointer_cast<RenderSubPass>(workflowResults->commands[j][i]);
      if (subpass == nullptr || subpass->renderPass == nullptr)
        continue;
      auto rpit = std::find(begin(renderPasses), end(renderPasses), subpass->renderPass);
      if (rpit == end(renderPasses))
        renderPasses.push_back(subpass->renderPass);

      auto transitions = workflow.getOperationIO(workflowResults->commands[j][i]->operation->name, rttAttachmentOutput | rttAttachmentResolveOutput);
      for (auto& transition : transitions)
        if (transition->resource->resourceType->attachment.attachmentType == atSurface)
          workflowResults->presentationQueueIndex = j;
    }
  }

  // build framebuffers
  for (auto& renderPass : renderPasses)
  {
    std::map<std::string,uint32_t>          definedImages;
    std::vector<FrameBufferImageDefinition> frameBufferDefinitions;
    std::vector<VkImageLayout>              rpInitialLayouts;
    for (auto& sb : renderPass->subPasses)
    {
      auto subPass           = sb.lock();
      uint32_t opid          = operationMap.at(subPass->operation->name);
      auto transitions       = workflow.getOperationIO(subPass->operation->name, rttAllAttachments);
      auto opLayouts         = allLayouts[opid];
      for (auto& transition : transitions)
      {
        VkExtent3D imSize{ 1,1,1 };
        auto resourceName   = workflowResults->resourceAlias.at(transition->resource->name);
        uint32_t resid      = resourceMap.at(resourceName);
        auto resourceType   = transition->resource->resourceType;
        auto aspectMask     = getAspectMask(resourceType->attachment.attachmentType);
        uint32_t layerCount = static_cast<uint32_t>(resourceType->attachment.attachmentSize.imageSize.z);
        auto ait            = workflowResults->registeredMemoryImages.find(resourceName);
        if (ait == end(workflowResults->registeredMemoryImages))
        {
          ImageTraits imageTraits(resourceType->attachment.imageUsage, resourceType->attachment.format, imSize, 1, layerCount, resourceType->attachment.samples, false, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE);
          SwapChainImageBehaviour scib = (resourceType->attachment.attachmentType == atSurface) ? swForEachImage : swOnce;
          ait = workflowResults->registeredMemoryImages.insert({ resourceName, std::make_shared<MemoryImage>(imageTraits, workflow.frameBufferAllocator, aspectMask, pbPerSurface, scib, false, false) }).first;
        }
        auto aiv = workflowResults->registeredImageViews.find(resourceName);
        if (aiv == end(workflowResults->registeredImageViews))
        {
          ImageSubresourceRange range(aspectMask, 0, 1, 0, layerCount);
          VkImageViewType imageViewType = (layerCount > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
          aiv = workflowResults->registeredImageViews.insert({ resourceName, std::make_shared<ImageView>(ait->second, range, imageViewType) }).first;
          workflowResults->initialImageLayouts.insert({ resourceName, std::make_tuple(opLayouts[resid], resourceType->attachment.attachmentType , aspectMask) });
        }
        if (definedImages.find(resourceName) == end(definedImages))
        {
          rpInitialLayouts.push_back(opLayouts[resid]);
          definedImages.insert({ resourceName,static_cast<uint32_t>(frameBufferDefinitions.size()) });
          frameBufferDefinitions.push_back(FrameBufferImageDefinition(
            resourceType->attachment.attachmentType,
            resourceType->attachment.format,
            resourceType->attachment.imageUsage,
            aspectMask,
            resourceType->attachment.samples,
            resourceName,
            resourceType->attachment.attachmentSize,
            resourceType->attachment.swizzles
          ));
        }
      }
    }

    AttachmentSize frameBufferSize;
    if (!renderPass->subPasses.empty())
      frameBufferSize = renderPass->subPasses[0].lock()->operation->attachmentSize;
    auto frameBuffer = std::make_shared<FrameBuffer>(frameBufferSize, frameBufferDefinitions, renderPass, workflowResults->registeredMemoryImages, workflowResults->registeredImageViews);
    workflowResults->frameBuffers.push_back(frameBuffer);

    // build attachments, clear values and image layouts
    std::vector<AttachmentDefinition> attachments;
    std::vector<VkClearValue>         clearValues(frameBufferDefinitions.size(), makeColorClearValue(glm::vec4(0.0f)));
    std::vector<char>                 clearValuesInitialized(frameBufferDefinitions.size(), false);
    for (uint32_t i = 0; i < frameBufferDefinitions.size(); ++i)
    {
      attachments.push_back(AttachmentDefinition(
        i,
        frameBufferDefinitions[i].format,
        frameBufferDefinitions[i].samples,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        rpInitialLayouts[i],
        rpInitialLayouts[i],
        0
      ));
    }

    // find all information about attachments and clear values
    for (auto& sb : renderPass->subPasses)
    {
      auto subPass   = sb.lock();
      uint32_t opid  = operationMap.at(subPass->operation->name);
      auto opLayouts = allLayouts[opid];

      // fill attachment information with render subpass specifics ( initial layout, final layout, load op, clear values )
      auto transitions = workflow.getOperationIO(subPass->operation->name, rttAllAttachments);
      for (auto& transition : transitions)
      {
        auto resourceName = workflowResults->resourceAlias.at(transition->resource->name);
        uint32_t attIndex = definedImages.at(resourceName);

        frameBufferDefinitions[attIndex].usage |= getAttachmentUsage(transition->layout);
        attachments[attIndex].finalLayout      = transition->layout;
        AttachmentType at                      = transition->resource->resourceType->attachment.attachmentType;
        bool colorDepthAttachment              = (at == atSurface) || (at == atColor) || (at == atDepth) || (at == atDepthStencil);
        bool stencilAttachment                 = (at == atDepthStencil) || (at == atStencil);
        bool stencilDepthAttachment            = (at == atDepth) || (at == atDepthStencil) || (at == atStencil);

        // if it's an output transition
        if ((transition->transitionType & rttAllOutputs) != 0)
        {
          if (attachments[attIndex].initialLayout == VK_IMAGE_LAYOUT_UNDEFINED)
            attachments[attIndex].initialLayout = transition->layout;
        }

        // if it's an input transition
        if ((transition->transitionType & rttAllInputs) != 0)
        {
          // FIXME
        }

        if (attachments[attIndex].loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
          attachments[attIndex].loadOp = colorDepthAttachment ? (VkAttachmentLoadOp)transition->load.loadType : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        if (attachments[attIndex].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
          attachments[attIndex].stencilLoadOp = stencilAttachment ? (VkAttachmentLoadOp)transition->load.loadType : VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        if (!clearValuesInitialized[attIndex])
        {
          if (stencilDepthAttachment)
            clearValues[attIndex] = makeDepthStencilClearValue(transition->load.clearColor.x, transition->load.clearColor.y);
          else
            clearValues[attIndex] = makeColorClearValue(transition->load.clearColor);
          clearValuesInitialized[attIndex] = true;
        }
      }
    }

    // build subpass definitions
    for (auto& sb : renderPass->subPasses)
    {
      auto subPass = sb.lock();
      VkPipelineBindPoint              bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      std::vector<AttachmentReference> ia;
      std::vector<AttachmentReference> oa;
      std::vector<AttachmentReference> ra;
      AttachmentReference              dsa;
      std::vector<uint32_t>            pa;

      auto inputAttachments   = workflow.getOperationIO(subPass->operation->name, rttAttachmentInput);
      auto outputAttachments  = workflow.getOperationIO(subPass->operation->name, rttAttachmentOutput);
      auto resolveAttachments = workflow.getOperationIO(subPass->operation->name, rttAttachmentResolveOutput);
      auto depthAttachments   = workflow.getOperationIO(subPass->operation->name, rttAttachmentDepthInput | rttAttachmentDepthOutput);

      for (auto& inputAttachment : inputAttachments)
        ia.push_back({ definedImages.at(workflowResults->resourceAlias.at(inputAttachment->resource->name)), inputAttachment->layout });
      for (auto& outputAttachment : outputAttachments)
      {
        oa.push_back({ definedImages.at(workflowResults->resourceAlias.at(outputAttachment->resource->name)), outputAttachment->layout });

        if (!resolveAttachments.empty())
        {
          auto it = std::find_if(begin(resolveAttachments), end(resolveAttachments), [outputAttachment](const std::shared_ptr<ResourceTransition>& rt) -> bool { return rt->resolveResource == outputAttachment->resource; });
          if (it != end(resolveAttachments))
            ra.push_back({ definedImages.at(workflowResults->resourceAlias.at((*it)->resource->name)), (*it)->layout });
          else
            ra.push_back({ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
        }
        else
          ra.push_back({ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
      }
      if (!depthAttachments.empty())
        dsa = { definedImages.at(workflowResults->resourceAlias.at(depthAttachments[0]->resource->name)), depthAttachments[0]->layout };
      else
        dsa = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };

      SubpassDefinition subPassDefinition(VK_PIPELINE_BIND_POINT_GRAPHICS, ia, oa, ra, dsa, pa, 0, subPass->operation->multiViewMask);

      // OK, so we have a subpass definition - the one thing that's missing is information about preserved attachments ( in a subpass ) and attachments that must be saved ( in a render pass )

      auto inTransitions  = workflow.getOperationIO(subPass->operation->name, rttAllAttachmentInputs);
      auto outTransitions = workflow.getOperationIO(subPass->operation->name, rttAllAttachmentOutputs);
      auto thisOperation  = std::find(begin(partialOrdering), end(partialOrdering), subPass->operation);
      bool lastSubpass    = ((subPass->subpassIndex + 1) == renderPass->subPasses.size());

      // check which resources are used in a subpass
      std::set<std::string> subpassResourceNames;
      for (auto& inTransition : inTransitions)
        subpassResourceNames.insert(inTransition->resource->name);
      for (auto& outTransition : outTransitions)
        subpassResourceNames.insert(outTransition->resource->name);
      // for each unused resource : it must be preserved when
      // - it is persistent
      // - it is swapchain image
      // - it was used before
      // - it is used later in a subpass or outside 
      for (const auto& resName : workflow.getResourceNames())
      {
        // FIXME
        auto resource          = workflow.getResource(resName);
        if (resource->resourceType->metaType != RenderWorkflowResourceType::Attachment)
          continue;
        if (definedImages.find(workflowResults->resourceAlias.at(resName)) == end(definedImages))
          continue;
        auto resOutTransitions = workflow.getResourceIO(resName, rttAllOutputs);
        auto resInTransitions  = workflow.getResourceIO(resName, rttAllInputs);

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
        bool usedNow               = subpassResourceNames.find(resName) != end(subpassResourceNames) || subpassResourceNames.find(workflowResults->resourceAlias.at(resName)) != end(subpassResourceNames);
        bool preserve              = usedBefore && !usedNow && (usedLater || isSurfaceOrPersistent);
        bool save                  = lastSubpass && (usedLater || isSurfaceOrPersistent);
        uint32_t attIndex          = definedImages.at(workflowResults->resourceAlias.at(resName));
        if (preserve)
          subPassDefinition.preserveAttachments.push_back(attIndex);
        if (save)
        {
          AttachmentType at = resource->resourceType->attachment.attachmentType;
          bool colorDepthAttachment = (at == atSurface) || (at == atColor) || (at == atDepth) || (at == atDepthStencil);
          bool stencilAttachment = (at == atDepthStencil) || (at == atStencil);

          if (colorDepthAttachment) attachments[attIndex].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
          if (stencilAttachment)    attachments[attIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
      }
      subPass->setSubPassDefinition(subPassDefinition);
    }
    renderPass->setRenderPassData(frameBuffer, attachments, clearValues);
  }
}

void SingleQueueWorkflowCompiler::createPipelineBarriers(const RenderWorkflow& workflow, std::vector<std::vector<std::shared_ptr<RenderCommand>>>& commandSequences, std::shared_ptr<RenderWorkflowResults> workflowResults)
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
      if( ((generatingTransitions[0]->transitionType & rttAllAttachmentOutputs) != 0) && ((consumingTransition->transitionType & rttAllAttachmentInputs) != 0) )
        createSubpassDependency(generatingTransitions[0], commandMap[generatingTransitions[0]->operation->name], consumingTransition, commandMap[consumingTransition->operation->name], queueNumber[generatingTransitions[0]->operation->name], queueNumber[consumingTransition->operation->name], workflowResults);
      else
        createPipelineBarrier(generatingTransitions[0], commandMap[generatingTransitions[0]->operation->name], consumingTransition, commandMap[consumingTransition->operation->name], queueNumber[generatingTransitions[0]->operation->name], queueNumber[consumingTransition->operation->name], workflowResults);
    }
  }
}

void SingleQueueWorkflowCompiler::createSubpassDependency(std::shared_ptr<ResourceTransition> generatingTransition, std::shared_ptr<RenderCommand> generatingCommand, std::shared_ptr<ResourceTransition> consumingTransition, std::shared_ptr<RenderCommand> consumingCommand, uint32_t generatingQueueIndex, uint32_t consumingQueueIndex, std::shared_ptr<RenderWorkflowResults> workflowResults)
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
    createPipelineBarrier(generatingTransition, generatingCommand, consumingTransition, consumingCommand, generatingQueueIndex, consumingQueueIndex, workflowResults);
}

void SingleQueueWorkflowCompiler::createPipelineBarrier(std::shared_ptr<ResourceTransition> generatingTransition, std::shared_ptr<RenderCommand> generatingCommand, std::shared_ptr<ResourceTransition> consumingTransition, std::shared_ptr<RenderCommand> consumingCommand, uint32_t generatingQueueIndex, uint32_t consumingQueueIndex, std::shared_ptr<RenderWorkflowResults> workflowResults)
{
  auto workflow = generatingTransition->operation->renderWorkflow.lock();

  // If there's no associated memory object then there can be no pipeline barrier
  // Some inputs/outputs may be added without memory objects just to enforce proper order of operations
  auto memoryObject = workflow->getAssociatedMemoryObject(generatingTransition->resource->name);
  if (memoryObject == nullptr)
  {
    auto it = workflowResults->registeredMemoryImages.find(workflowResults->resourceAlias.at(generatingTransition->resource->name));
    if (it == end(workflowResults->registeredMemoryImages))
      return;
    memoryObject = it->second;
  }

  uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  if (generatingQueueIndex != consumingQueueIndex)
  {
    // TODO - find family indices for both queues
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
    auto bufferRange = generatingTransition->bufferSubresourceRange;
    rbgit->second.push_back(MemoryObjectBarrier(srcAccessMask, dstAccessMask, srcQueueFamilyIndex, dstQueueFamilyIndex, memoryObject, bufferRange));
    break;
  }
  case RenderWorkflowResourceType::Image:
  case RenderWorkflowResourceType::Attachment:
  {
    VkImageLayout oldLayout = generatingTransition->layout;
    VkImageLayout newLayout = consumingTransition->layout;
    auto imageRange         = generatingTransition->imageSubresourceRange;
    rbgit->second.push_back(MemoryObjectBarrier(srcAccessMask, dstAccessMask, srcQueueFamilyIndex, dstQueueFamilyIndex, memoryObject, oldLayout, newLayout, imageRange));
    break;
  }
  default:
    break;
  }
}
