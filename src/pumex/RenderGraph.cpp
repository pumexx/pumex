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

#include <pumex/RenderGraph.h>
#include <algorithm>
#include <pumex/utils/Log.h>

using namespace pumex;

AttachmentDefinition::AttachmentDefinition()
  : format{ VK_FORMAT_UNDEFINED }, attachmentType{ atUndefined }, attachmentSize{}, swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA)
{
}

AttachmentDefinition::AttachmentDefinition(VkFormat f, const ImageSize& as, AttachmentType at, const gli::swizzles& sw)
  : format{ f }, attachmentSize{ as }, attachmentType{ at }, swizzles{ sw }
{
}

ResourceDefinition::ResourceDefinition()
  : metaType{ rmtUndefined }, attachment{}
{
}

ResourceDefinition::ResourceDefinition(VkFormat f, const ImageSize& as, AttachmentType at, const std::string& n, const gli::swizzles& sw)
  : metaType{ rmtImage }, attachment{ f, as, at, sw }, name{ n }
{
}

ResourceDefinition::ResourceDefinition(const std::string& n)
  : metaType{ rmtBuffer }, name{ n }
{
  CHECK_LOG_THROW(name.empty(), "ResourceDefinition : all buffers must have a name defined");
}

RenderOperationEntry::RenderOperationEntry(OperationEntryType et, const ResourceDefinition& rd, const LoadOp& lop, const ImageSubresourceRange& ir, VkImageLayout il, VkImageUsageFlags iu, const std::string& rse)
  : entryType{ et }, resourceDefinition{ rd }, loadOp{ lop }, imageRange{ ir }, imageUsage{ iu }, resolveSourceEntryName{ rse }
{
  switch (entryType)
  {
  case opeAttachmentInput:         layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;         break;
  case opeAttachmentOutput:        layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;         break;
  case opeAttachmentResolveOutput: layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;         break;
  case opeAttachmentDepthOutput:   layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; break;
  case opeAttachmentDepthInput:    layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;  break;
  default:                         layout = il; break;
  }
}

RenderOperationEntry::RenderOperationEntry(OperationEntryType et, const ResourceDefinition& rd, const BufferSubresourceRange& bufferRange, VkPipelineStageFlags pipelineStage, VkAccessFlags accessFlags)
  : entryType{ et }, resourceDefinition{ rd }
{
}

RenderOperation::RenderOperation()
{
}

RenderOperation::RenderOperation(const std::string& n, OperationType t, const ImageSize& as, uint32_t mvm )
  : name{ n }, operationType{ t }, attachmentSize{ as }, multiViewMask{ mvm }
{
}

void RenderOperation::addAttachmentInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment input is not an image : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.attachment.attachmentSize != attachmentSize, "RenderOperation : Attachment must have the same size as its operation : " << name << ":" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentInput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, 0, std::string() } });
  //valid = false;
}

void RenderOperation::addAttachmentOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment output is not an image : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.attachment.attachmentSize != attachmentSize, "RenderOperation : Attachment must have the same size as its operation : " << name << ":" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentOutput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, 0, std::string() } });
  //valid = false;
}

void RenderOperation::addAttachmentResolveOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, const std::string& sourceEntryName)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment output is not an image : " << name << ":" << entryName);
  // compare operation size and attachment size skipping samples
  auto compareSize = resourceDefinition.attachment.attachmentSize;
  compareSize.samples = attachmentSize.samples;
  CHECK_LOG_THROW(compareSize != attachmentSize, "RenderOperation : Attachment must have the same size as its operation : " << name << ":" << entryName);
  CHECK_LOG_THROW(sourceEntryName.empty(), "RenderOperation : Resolve source entry not defined : " << name << " : " << entryName);
  auto sourceEntry = entries.find(sourceEntryName);
  CHECK_LOG_THROW(sourceEntry == end(entries), "RenderOperation : Resolve source entry does not exist : " << name << ":" << entryName << "(" <<sourceEntryName << ")" );
  CHECK_LOG_THROW(sourceEntry->second.resourceDefinition.metaType != rmtImage, "RenderOperation : Resolve source entry is not an image : " << name << ":" << entryName << "(" << sourceEntryName << ")");

  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentResolveOutput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, 0, sourceEntryName } });
  //valid = false;
}

void RenderOperation::setAttachmentDepthInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment depth input is not an image : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.attachment.attachmentSize != attachmentSize, "RenderOperation : Attachment must have the same size as its operation : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.attachment.attachmentType != atDepth &&
    resourceDefinition.attachment.attachmentType != atDepthStencil &&
    resourceDefinition.attachment.attachmentType != atStencil, "RenderOperation : Attachment type must be atDepth, atDepthStencil or atStencil : " << name << ":" << entryName);
  auto redundant = std::find_if(begin(entries), end(entries), [](std::pair<const std::string,RenderOperationEntry>& entry) { return (entry.second.entryType == opeAttachmentDepthInput || entry.second.entryType == opeAttachmentDepthOutput); });
  CHECK_LOG_THROW(redundant!= end(entries), "RenderOperation : There must be only one depth input or output : " << name << ":" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentDepthInput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, 0, std::string() } });
  //valid = false;
}

void RenderOperation::setAttachmentDepthOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment depth output is not an image : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.attachment.attachmentSize != attachmentSize, "RenderOperation : Attachment must have the same size as its operation : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.attachment.attachmentType != atDepth &&
    resourceDefinition.attachment.attachmentType != atDepthStencil &&
    resourceDefinition.attachment.attachmentType != atStencil, "RenderOperation : Attachmenmt type must be atDepth, atDepthStencil or atStencil : " << name << ":" << entryName);
  auto redundant = std::find_if(begin(entries), end(entries), [](std::pair<const std::string, RenderOperationEntry>& entry) { return (entry.second.entryType == opeAttachmentDepthInput || entry.second.entryType == opeAttachmentDepthOutput); });
  CHECK_LOG_THROW(redundant != end(entries), "RenderOperation : There must be only one depth input or output : " << name << ":" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentDepthOutput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, 0,  std::string() } });
  //valid = false;
}

void RenderOperation::addImageInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageLayout layout, VkImageUsageFlags imageUsage )
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as image input is not an image : " << name << ":" << entryName);

  entries.insert({ entryName, RenderOperationEntry{ opeImageInput, resourceDefinition, loadOp, imageRange, layout, imageUsage, std::string() } });
  //valid = false;
}

void RenderOperation::addImageOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageLayout layout, VkImageUsageFlags imageUsage )
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as image output is not an image : " << name << ":" << entryName);

  entries.insert({ entryName, RenderOperationEntry{ opeImageOutput, resourceDefinition, loadOp, imageRange, layout, imageUsage, std::string() } });
  //valid = false;
}

void RenderOperation::addBufferInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const BufferSubresourceRange& bufferRange, VkPipelineStageFlags pipelineStage, VkAccessFlags accessFlags)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtBuffer, "RenderOperation : Resource used as buffer input is not a buffer : " << name << ":" << entryName);

  entries.insert({ entryName, RenderOperationEntry{ opeBufferInput, resourceDefinition, bufferRange, pipelineStage, accessFlags } });
  //valid = false;
}

void RenderOperation::addBufferOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const BufferSubresourceRange& bufferRange, VkPipelineStageFlags pipelineStage, VkAccessFlags accessFlags)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << ":" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtBuffer, "RenderOperation : Resource used as buffer output is not a buffer : " << name << ":" << entryName);

  entries.insert({ entryName, RenderOperationEntry{ opeBufferOutput, resourceDefinition, bufferRange, pipelineStage, accessFlags } });
  //valid = false;
}

void RenderOperation::setRenderOperationNode(std::shared_ptr<Node> n)
{
  node = n;
}

std::shared_ptr<Node> RenderOperation::getRenderOperationNode()
{
  return node;
}

std::vector<std::reference_wrapper<const RenderOperationEntry>> RenderOperation::getEntries(OperationEntryTypeFlags entryTypes) const
{
  std::vector<std::reference_wrapper<const RenderOperationEntry>> results;
  for (auto& entry : entries)
  {
    if (entry.second.entryType & entryTypes)
      results.push_back(entry.second);
  }
  return results;
}


ResourceTransition::ResourceTransition(uint32_t id, const std::map<std::string, RenderOperation>::const_iterator& op, const std::map<std::string, RenderOperationEntry>::const_iterator& e, const std::string& emon)
  : id_{ id }, operation_{ op }, entry_{ e }, externalMemoryObjectName_{ emon }
{
}

RenderGraph::RenderGraph(const std::string& n)
  : name{ n }
{
}

RenderGraph::~RenderGraph()
{
}

void RenderGraph::addRenderOperation(const RenderOperation& op)
{
  auto it = operations.find(op.name);
  CHECK_LOG_THROW(it != end(operations), "RenderGraph : operation already exists : " + op.name);
  operations.insert({ op.name, op });
  valid = false;
}

void RenderGraph::addResourceTransition(const std::string& generatingOperation, const std::string& generatingEntry, const std::string& consumingOperation, const std::string& consumingEntry, const std::string& externalMemoryObjectName)
{
  CHECK_LOG_THROW(generatingOperation == consumingOperation, "RenderGraph : generating and consuming operation is the same : " << generatingOperation);
  auto genOp = operations.find(generatingOperation);
  CHECK_LOG_THROW(genOp == end(operations), "RenderGraph : generating operation not defined : " << generatingOperation);
  auto conOp = operations.find(consumingOperation);
  CHECK_LOG_THROW(conOp == end(operations), "RenderGraph : consuming operation not defined : " << generatingOperation);

  auto genEntry = genOp->second.entries.find(generatingEntry);
  CHECK_LOG_THROW(genEntry == end(genOp->second.entries), "RenderGraph : operation " << generatingOperation <<  " does not have entry named : " << generatingEntry);
  CHECK_LOG_THROW((genEntry->second.entryType & opeAllOutputs) == 0, "RenderGraph : entry " << generatingOperation << "->" << generatingEntry << " is not an output");

  auto conEntry = conOp->second.entries.find(consumingEntry);
  CHECK_LOG_THROW(conEntry == end(conOp->second.entries), "RenderGraph : operation " << consumingOperation << " does not have entry named : " << consumingEntry);
  CHECK_LOG_THROW((conEntry->second.entryType & opeAllInputs) == 0, "RenderGraph : entry " << consumingOperation << "->" << consumingEntry << " is not an input");

  // both entries must have the same resource definition
  CHECK_LOG_THROW(!(genEntry->second.resourceDefinition == conEntry->second.resourceDefinition), "RenderGraph : entries " << generatingOperation << "->" << generatingEntry << " and " << consumingOperation << "->" << consumingEntry << " must have the same resource definition");

  // consuming entry must have at most one input
  auto existingConTransition = std::find_if(begin(transitions), end(transitions), [conOp, conEntry](const ResourceTransition& rt) { return (rt.operationIter() == conOp && rt.entryIter() == conEntry); });
  CHECK_LOG_THROW(existingConTransition != end(transitions), "RenderGraph : Entry " << consumingOperation << ":" << consumingEntry << " cannot have more than one input");

  // generating entry may have zero or more outputs
  auto existingGenTransition = std::find_if(begin(transitions), end(transitions), [genOp, genEntry](const ResourceTransition& rt) { return (rt.operationIter() == genOp && rt.entryIter() == genEntry); });
  uint32_t transitionID;
  if (existingGenTransition != end(transitions))
  {
    // all transitions using the same output must have the same external resource defined
    CHECK_LOG_THROW(existingGenTransition->externalMemoryObjectName() != externalMemoryObjectName, "RenderGraph : All transitions using " << generatingOperation << "->" << generatingEntry << " must have the same external resource : " << existingGenTransition->externalMemoryObjectName() << " != " << externalMemoryObjectName);
    transitionID = existingGenTransition->id();
  }
  else
  {
    transitionID = generateTransitionID();
    transitions.push_back(ResourceTransition(transitionID, genOp, genEntry, externalMemoryObjectName));
  }
  transitions.push_back(ResourceTransition(transitionID, conOp, conEntry, externalMemoryObjectName));
  valid = false;
}

void RenderGraph::addResourceTransition(const std::string& opName, const std::string& entryName, const std::string& externalMemoryObjectName)
{
  auto op = operations.find(opName);
  CHECK_LOG_THROW(op == end(operations), "RenderGraph : generating operation not defined : " << opName);

  auto entry = op->second.entries.find(entryName);
  CHECK_LOG_THROW(entry == end(op->second.entries), "RenderGraph : operation " << opName << " does not have entry named : " << entryName);

  auto existingTransition = std::find_if(begin(transitions), end(transitions), [op, entry](const ResourceTransition& rt) { return (rt.operationIter() == op && rt.entryIter() == entry); });
  if (existingTransition != end(transitions))
  {
    LOG_WARNING << "RenderGraph : operation " << opName << " already has entry named : " << entryName << " connected to a transition. Overwriting externalMemoryObjectName" << std::endl;
    existingTransition->setExternalMemoryObjectName(externalMemoryObjectName);
    return;
  }
  uint32_t transitionID = generateTransitionID();
  transitions.push_back(ResourceTransition(transitionID, op, entry, externalMemoryObjectName));
  valid = false;
}

void RenderGraph::addMissingResourceTransitions()
{
  for (auto opit = begin(operations); opit != end(operations); ++opit)
  {
    std::vector<ResourceTransition> emptyTransitions;
    auto opTransitions = getOperationIO(opit->first, opeAllInputsOutputs);
    for (auto opeit = begin(opit->second.entries); opeit != end(opit->second.entries); ++opeit)
    {
      if (std::none_of(begin(opTransitions), end(opTransitions), [&opeit](const ResourceTransition& tr) { return opeit->first == tr.entryName(); }))
      {
        uint32_t transitionID = generateTransitionID();
        emptyTransitions.push_back(ResourceTransition(transitionID, opit, opeit, std::string()));
      }
    }
    std::copy(begin(emptyTransitions), end(emptyTransitions), std::back_inserter(transitions));
  }
}

std::vector<std::string> RenderGraph::getRenderOperationNames() const
{
  std::vector<std::string> results;
  for (auto& op : operations)
    results.push_back(op.first);
  return results;
}

const RenderOperation& RenderGraph::getRenderOperation(const std::string& opName) const
{
  auto it = operations.find(opName);
  CHECK_LOG_THROW(it == end(operations), "RenderGraph : there is no operation with name " + opName);
  return it->second;
}

RenderOperation& RenderGraph::getRenderOperation(const std::string& opName)
{
  auto it = operations.find(opName);
  CHECK_LOG_THROW(it == end(operations), "RenderGraph : there is no operation with name " + opName);
  return it->second;
}

void RenderGraph::setRenderOperationNode(const std::string& opName, std::shared_ptr<Node> n)
{
  getRenderOperation(opName).setRenderOperationNode( n );
  valid = false;
}

std::shared_ptr<Node> RenderGraph::getRenderOperationNode(const std::string& opName)
{
  return getRenderOperation(opName).node;
}

std::vector<std::reference_wrapper<const ResourceTransition>> RenderGraph::getOperationIO(const std::string& opName, OperationEntryTypeFlags entryTypes) const
{
  std::vector<std::reference_wrapper<const ResourceTransition>> results;
  std::copy_if(begin(transitions), end(transitions), std::back_inserter(results),
    [opName, entryTypes](const ResourceTransition& c)->bool { return c.operationName() == opName && (c.entry().entryType & entryTypes); });
  return results;
}

std::vector<std::reference_wrapper<const ResourceTransition>> RenderGraph::getTransitionIO(uint32_t transitionID, OperationEntryTypeFlags entryTypes) const
{
  std::vector<std::reference_wrapper<const ResourceTransition>> results;
  std::copy_if(begin(transitions), end(transitions), std::back_inserter(results),
    [transitionID, entryTypes](const ResourceTransition& c)->bool { return c.id() == transitionID && (c.entry().entryType & entryTypes); });
  return results;
}

uint32_t RenderGraph::generateTransitionID()
{
  auto result = nextTransitionID++;
  return result;
}

namespace pumex
{

ResourceDefinition SWAPCHAIN_DEFINITION(VkFormat format, uint32_t arrayLayers)
{
  return ResourceDefinition(format, ImageSize{ isSurfaceDependent, glm::vec2(1.0f,1.0f), arrayLayers, 1, 1 }, atColor);
}

RenderOperationSet getInitialOperations(const RenderGraph& renderGraph)
{
  // operation is initial when there are no input resources
  RenderOperationSet initialOperations;
  for (auto& it : renderGraph.operations)
  {
    auto inTransitions = renderGraph.getOperationIO(it.second.name, opeAllInputs);
    if(inTransitions.empty())
      initialOperations.insert(it.second);
  }
  return initialOperations;
}

RenderOperationSet getFinalOperations(const RenderGraph& renderGraph)
{
  // operation is final when there are no output resources
  RenderOperationSet finalOperations;
  for (auto& it : renderGraph.operations)
  {
    auto outTransitions = renderGraph.getOperationIO(it.second.name, opeAllOutputs);
    if (outTransitions.empty())
      finalOperations.insert(it.second);
  }
  return finalOperations;
}

RenderOperationSet getPreviousOperations(const RenderGraph& renderGraph, const std::string& opName)
{
  auto inTransitions = renderGraph.getOperationIO(opName, opeAllInputs);
  RenderOperationSet previousOperations;
  for (auto& inTransition : inTransitions)
  {
    // operation is final if all of its ouputs are inputs for final operations
    auto transitions = renderGraph.getTransitionIO(inTransition.get().id(), opeAllOutputs);
    for (auto transition : transitions)
      previousOperations.insert(transition.get().operation());
  }
  return previousOperations;
}

RenderOperationSet getNextOperations(const RenderGraph& renderGraph, const std::string& opName)
{
  auto outTransitions = renderGraph.getOperationIO(opName, opeAllOutputs);
  RenderOperationSet nextOperations;
  for (auto& outTransition : outTransitions)
  {
    // operation is final if all of its ouputs are inputs for final operations
    auto transitions = renderGraph.getTransitionIO(outTransition.get().id(), opeAllInputs);
    for (auto transition : transitions)
      nextOperations.insert(transition.get().operation());
  }
  return nextOperations;
}

RenderOperationSet getAllPreviousOperations(const RenderGraph& renderGraph, const std::string& opName)
{
  RenderOperationSet results;

  auto prevOperations = getPreviousOperations(renderGraph, opName);

  while (!prevOperations.empty())
  {
    decltype(prevOperations) prevOperations2;
    for (auto operation : prevOperations)
    {
      results.insert(operation);
      auto x = getPreviousOperations(renderGraph, operation.get().name);
      std::copy(begin(x), end(x), std::inserter(prevOperations2, end(prevOperations2)));
    }
    prevOperations = prevOperations2;
  }
  return results;
}

RenderOperationSet getAllNextOperations(const RenderGraph& renderGraph, const std::string& opName)
{
  RenderOperationSet results;

  auto nextOperations = getNextOperations(renderGraph, opName);

  while (!nextOperations.empty())
  {
    decltype(nextOperations) nextOperations2;
    for (auto operation : nextOperations)
    {
      results.insert(operation);
      auto x = getNextOperations(renderGraph, operation.get().name);
      std::copy(begin(x), end(x), std::inserter(nextOperations2, end(nextOperations2)));
    }
    nextOperations = nextOperations2;
  }
  return results;
}

}
