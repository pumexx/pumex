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

RenderOperationEntry::RenderOperationEntry(OperationEntryType et, const ResourceDefinition& rd, const LoadOp& lop, const ImageSubresourceRange& ir, VkImageLayout il, VkImageUsageFlags iu, VkImageCreateFlags ic, VkImageViewType ivt , const std::string& rse, bool sa)
  : entryType{ et }, resourceDefinition{ rd }, loadOp{ lop }, imageRange{ ir }, imageUsage{ iu }, imageCreate{ ic }, imageViewType{ ivt }, resolveSourceEntryName{ rse }, storeAttachment{ sa }
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

RenderOperationEntry::RenderOperationEntry(OperationEntryType et, const ResourceDefinition& rd, const BufferSubresourceRange& br, VkPipelineStageFlags ps, VkAccessFlags af, VkFormat bf)
  : entryType{ et }, resourceDefinition{ rd }, bufferRange{ br }, pipelineStage{ ps }, accessFlags{ af }, bufferFormat{ bf }
{
}

RenderOperation::RenderOperation()
{
}

RenderOperation::RenderOperation(const std::string& n, OperationType t, const ImageSize& as, uint32_t mvm )
  : name{ n }, operationType{ t }, attachmentSize{ as }, multiViewMask{ mvm }
{
}

void RenderOperation::addAttachmentInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageCreateFlags imageCreate)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment input is not an image : " << name << "->" << entryName);
  CHECK_LOG_THROW(!compareImageSizeSkipArrays(resourceDefinition.attachment.attachmentSize, attachmentSize), "RenderOperation : Attachment must have the same size as its operation : " << name << "->" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentInput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, 0, imageCreate, VK_IMAGE_VIEW_TYPE_MAX_ENUM, std::string(), false } });
  //valid = false;
}

void RenderOperation::addAttachmentOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageCreateFlags imageCreate, bool storeAttachment)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment output is not an image : " << name << "->" << entryName);
  //CHECK_LOG_THROW(!compareImageSizeSkipArrays(resourceDefinition.attachment.attachmentSize, attachmentSize), "RenderOperation : Attachment must have the same size as its operation : " << name << "->" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentOutput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, 0, imageCreate, VK_IMAGE_VIEW_TYPE_MAX_ENUM, std::string(), storeAttachment } });
  //valid = false;
}

void RenderOperation::addAttachmentResolveOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageCreateFlags imageCreate, bool storeAttachment, const std::string& sourceEntryName)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment output is not an image : " << name << "->" << entryName);
  // compare operation size and attachment size skipping samples
  auto compareSize = resourceDefinition.attachment.attachmentSize;
  compareSize.samples = attachmentSize.samples;
  CHECK_LOG_THROW(!compareImageSizeSkipArrays(compareSize, attachmentSize), "RenderOperation : Attachment must have the same size as its operation : " << name << "->" << entryName);
  CHECK_LOG_THROW(sourceEntryName.empty(), "RenderOperation : Resolve source entry not defined : " << name << " : " << entryName);
  auto sourceEntry = entries.find(sourceEntryName);
  CHECK_LOG_THROW(sourceEntry == end(entries), "RenderOperation : Resolve source entry does not exist : " << name << "->" << entryName << "(" <<sourceEntryName << ")" );
  CHECK_LOG_THROW(sourceEntry->second.resourceDefinition.metaType != rmtImage, "RenderOperation : Resolve source entry is not an image : " << name << "->" << entryName << "(" << sourceEntryName << ")");

  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentResolveOutput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, 0, imageCreate, VK_IMAGE_VIEW_TYPE_MAX_ENUM, sourceEntryName, storeAttachment } });
  //valid = false;
}

void RenderOperation::setAttachmentDepthInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageCreateFlags imageCreate)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment depth input is not an image : " << name << "->" << entryName);
  CHECK_LOG_THROW(!compareImageSizeSkipArrays(resourceDefinition.attachment.attachmentSize, attachmentSize), "RenderOperation : Attachment must have the same size as its operation : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.attachment.attachmentType != atDepth &&
    resourceDefinition.attachment.attachmentType != atDepthStencil &&
    resourceDefinition.attachment.attachmentType != atStencil, "RenderOperation : Attachment type must be atDepth, atDepthStencil or atStencil : " << name << "->" << entryName);
  auto redundant = std::find_if(begin(entries), end(entries), [](std::pair<const std::string,RenderOperationEntry>& entry) { return (entry.second.entryType == opeAttachmentDepthInput || entry.second.entryType == opeAttachmentDepthOutput); });
  CHECK_LOG_THROW(redundant!= end(entries), "RenderOperation : There must be only one depth input or output : " << name << "->" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentDepthInput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, 0, imageCreate, VK_IMAGE_VIEW_TYPE_MAX_ENUM, std::string(), false } });
  //valid = false;
}

void RenderOperation::setAttachmentDepthOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageCreateFlags imageCreate, bool storeAttachment)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment depth output is not an image : " << name << "->" << entryName);
  CHECK_LOG_THROW(!compareImageSizeSkipArrays(resourceDefinition.attachment.attachmentSize, attachmentSize), "RenderOperation : Attachment must have the same size as its operation : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.attachment.attachmentType != atDepth &&
    resourceDefinition.attachment.attachmentType != atDepthStencil &&
    resourceDefinition.attachment.attachmentType != atStencil, "RenderOperation : Attachmenmt type must be atDepth, atDepthStencil or atStencil : " << name << "->" << entryName);
  auto redundant = std::find_if(begin(entries), end(entries), [](std::pair<const std::string, RenderOperationEntry>& entry) { return (entry.second.entryType == opeAttachmentDepthInput || entry.second.entryType == opeAttachmentDepthOutput); });
  CHECK_LOG_THROW(redundant != end(entries), "RenderOperation : There must be only one depth input or output : " << name << "->" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentDepthOutput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, 0, imageCreate, VK_IMAGE_VIEW_TYPE_MAX_ENUM, std::string(), storeAttachment } });
  //valid = false;
}

void RenderOperation::addImageInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageLayout layout, VkImageUsageFlags imageUsage, VkImageCreateFlags imageCreate, VkImageViewType imageViewType)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as image input is not an image : " << name << "->" << entryName);

  entries.insert({ entryName, RenderOperationEntry{ opeImageInput, resourceDefinition, loadOp, imageRange, layout, imageUsage, imageCreate, imageViewType, std::string(), false } });
  //valid = false;
}

void RenderOperation::addImageOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageLayout layout, VkImageUsageFlags imageUsage, VkImageCreateFlags imageCreate, VkImageViewType imageViewType)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as image output is not an image : " << name << "->" << entryName);

  entries.insert({ entryName, RenderOperationEntry{ opeImageOutput, resourceDefinition, loadOp, imageRange, layout, imageUsage, imageCreate, imageViewType, std::string(), false } });
  //valid = false;
}

void RenderOperation::addBufferInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const BufferSubresourceRange& bufferRange, VkPipelineStageFlags pipelineStage, VkAccessFlags accessFlags)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtBuffer, "RenderOperation : Resource used as buffer input is not a buffer : " << name << "->" << entryName);

  entries.insert({ entryName, RenderOperationEntry{ opeBufferInput, resourceDefinition, bufferRange, pipelineStage, accessFlags } });
  //valid = false;
}

void RenderOperation::addBufferOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const BufferSubresourceRange& bufferRange, VkPipelineStageFlags pipelineStage, VkAccessFlags accessFlags)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtBuffer, "RenderOperation : Resource used as buffer output is not a buffer : " << name << "->" << entryName);

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

ResourceTransition::ResourceTransition(uint32_t rteid, uint32_t tid, const std::map<std::string, RenderOperation>::const_iterator& op, const std::map<std::string, RenderOperationEntry>::const_iterator& e, const std::string& emon)
  : rteid_{ rteid }, tid_{ tid }, operation_{ op }, entry_{ e }, externalMemoryObjectName_{ emon }
{
}

ResourceTransitionDescription::ResourceTransitionDescription(const std::string& gop, const std::string& gen, const std::string& cop, const std::string& cen)
  : generatingOperation{ gop }, generatingEntry{ gen }, consumingOperation{ cop }, consumingEntry{ cen }
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

void RenderGraph::addResourceTransition(const ResourceTransitionDescription& resTran, const std::string& externalMemoryObjectName)
{
  CHECK_LOG_THROW(resTran.generatingOperation == resTran.consumingOperation, "RenderGraph : generating and consuming operation can't be the same : " << resTran.generatingOperation);
  auto genOp = operations.find(resTran.generatingOperation);
  CHECK_LOG_THROW(genOp == end(operations), "RenderGraph : generating operation not defined : " << resTran.generatingOperation);
  auto conOp = operations.find(resTran.consumingOperation);
  CHECK_LOG_THROW(conOp == end(operations), "RenderGraph : consuming operation not defined : " << resTran.consumingOperation);

  auto genEntry = genOp->second.entries.find(resTran.generatingEntry);
  CHECK_LOG_THROW(genEntry == end(genOp->second.entries), "RenderGraph : operation " << resTran.generatingOperation << " does not have entry named : " << resTran.generatingEntry);
  CHECK_LOG_THROW((genEntry->second.entryType & opeAllOutputs) == 0, "RenderGraph : entry " << resTran.generatingOperation << "->" << resTran.generatingEntry << " is not an output");

  auto conEntry = conOp->second.entries.find(resTran.consumingEntry);
  CHECK_LOG_THROW(conEntry == end(conOp->second.entries), "RenderGraph : operation " << resTran.consumingOperation << " does not have entry named : " << resTran.consumingEntry);
  CHECK_LOG_THROW((conEntry->second.entryType & opeAllInputs) == 0, "RenderGraph : entry " << resTran.consumingOperation << "->" << resTran.consumingEntry << " is not an input");

  // both entries must have the same resource definition
  CHECK_LOG_THROW(!(genEntry->second.resourceDefinition == conEntry->second.resourceDefinition), "RenderGraph : entries " << resTran.generatingOperation << "->" << resTran.generatingEntry << " and " << resTran.consumingOperation << "->" << resTran.consumingEntry << " must have the same resource definition");

  // consuming entry must have at most one input
  auto existingConTransition = std::find_if(begin(transitions), end(transitions), [conOp, conEntry](const ResourceTransition& rt) { return (rt.operationIter() == conOp && rt.entryIter() == conEntry); });
  CHECK_LOG_THROW(existingConTransition != end(transitions), "RenderGraph : Entry " << resTran.consumingOperation << "->" << resTran.consumingEntry << " cannot have more than one input");

  // generating entry may have zero or more outputs
  auto existingGenTransition = std::find_if(begin(transitions), end(transitions), [genOp, genEntry](const ResourceTransition& rt) { return (rt.operationIter() == genOp && rt.entryIter() == genEntry); });
  uint32_t transitionID;
  if (existingGenTransition != end(transitions))
  {
    // all transitions using the same output must have the same external resource defined
    CHECK_LOG_THROW(existingGenTransition->externalMemoryObjectName() != externalMemoryObjectName, "RenderGraph : All transitions using " << resTran.generatingOperation << "->" << resTran.generatingEntry << " must have the same external resource : " << existingGenTransition->externalMemoryObjectName() << " != " << externalMemoryObjectName);
    transitionID = existingGenTransition->tid();
  }
  else
  {
    transitionID = generateTransitionID();
    transitions.push_back(ResourceTransition(generateTransitionEntryID(), transitionID, genOp, genEntry, externalMemoryObjectName));
  }
  transitions.push_back(ResourceTransition(generateTransitionEntryID(), transitionID, conOp, conEntry, externalMemoryObjectName));
  valid = false;
}

void RenderGraph::addResourceTransition(const std::string& generatingOperation, const std::string& generatingEntry, const std::string& consumingOperation, const std::string& consumingEntry, const std::string& externalMemoryObjectName)
{
  addResourceTransition({ generatingOperation, generatingEntry, consumingOperation, consumingEntry }, externalMemoryObjectName);
}

void RenderGraph::addResourceTransition(const std::vector<ResourceTransitionDescription>& resTrans, const std::string& externalMemoryObjectName)
{
  CHECK_LOG_THROW(resTrans.empty(), "No resource transition definitions have been provided to addResourceTransition()");
  auto firstGenOp    = operations.find(resTrans[0].generatingOperation);
  CHECK_LOG_THROW(firstGenOp == end(operations), "RenderGraph : generating operation not defined : " << resTrans[0].generatingOperation);
  auto firstGenEntry = firstGenOp->second.entries.find(resTrans[0].generatingEntry);
  CHECK_LOG_THROW(firstGenEntry == end(firstGenOp->second.entries), "RenderGraph : operation " << resTrans[0].generatingOperation << " does not have entry named : " << resTrans[0].generatingEntry);
  for (auto& resTran : resTrans)
  {
    CHECK_LOG_THROW(resTran.generatingOperation == resTran.consumingOperation, "RenderGraph : generating and consuming operation is the same : " << resTran.generatingOperation);
    auto genOp = operations.find(resTran.generatingOperation);
    CHECK_LOG_THROW(genOp == end(operations), "RenderGraph : generating operation not defined : " << resTran.generatingOperation);
    auto conOp = operations.find(resTran.consumingOperation);
    CHECK_LOG_THROW(conOp == end(operations), "RenderGraph : consuming operation not defined : " << resTran.consumingOperation);

    auto genEntry = genOp->second.entries.find(resTran.generatingEntry);
    CHECK_LOG_THROW(genEntry == end(genOp->second.entries), "RenderGraph : operation " << resTran.generatingOperation << " does not have entry named : " << resTran.generatingEntry);
    CHECK_LOG_THROW((genEntry->second.entryType & opeAllOutputs) == 0, "RenderGraph : entry " << resTran.generatingOperation << "->" << resTran.generatingEntry << " is not an output");

    auto conEntry = conOp->second.entries.find(resTran.consumingEntry);
    CHECK_LOG_THROW(conEntry == end(conOp->second.entries), "RenderGraph : operation " << resTran.consumingOperation << " does not have entry named : " << resTran.consumingEntry);
    CHECK_LOG_THROW((conEntry->second.entryType & opeAllInputs) == 0, "RenderGraph : entry " << resTran.consumingOperation << "->" << resTran.consumingEntry << " is not an input");

    // all entries must have the same resource definition
    CHECK_LOG_THROW(!(genEntry->second.resourceDefinition == firstGenEntry->second.resourceDefinition), "RenderGraph : entry " << resTran.generatingOperation << "->" << resTran.generatingEntry << "  has different resource definition");
    CHECK_LOG_THROW(!(conEntry->second.resourceDefinition == firstGenEntry->second.resourceDefinition), "RenderGraph : entry " << resTran.consumingOperation << "->" << resTran.consumingEntry << "  has different resource definition");

    // generating entries must be empty
    auto existingGenTransition = std::find_if(begin(transitions), end(transitions), [genOp, genEntry](const ResourceTransition& rt) { return (rt.operationIter() == genOp && rt.entryIter() == genEntry); });
    CHECK_LOG_THROW( existingGenTransition != end(transitions), "RenderGraph : operation " << resTran.generatingOperation << "->" << resTran.generatingEntry <<  " cannot have any generating transitions defined.");

    // all existing consuming entries must not overlap with a new one
    std::vector<ResourceTransition> existingConsumingTransitions;
    std::copy_if(begin(transitions), end(transitions), std::back_inserter(existingConsumingTransitions), [conOp, conEntry](const ResourceTransition& rt) { return (rt.operationIter() == conOp && rt.entryIter() == conEntry); });
    for (auto& ect : existingConsumingTransitions)
    {
      if ((conEntry->second.entryType & (opeAllAttachments | opeAllImages)) != 0)
      {
        CHECK_LOG_THROW(rangeOverlaps(ect.entry().imageRange, conEntry->second.imageRange), "RenderGraph : Entry " << resTran.consumingOperation << "->" << resTran.consumingEntry << " overlaps with entry " << ect.operationName() << "->" << ect.entryName());
      }
      else
        CHECK_LOG_THROW(rangeOverlaps(ect.entry().bufferRange, conEntry->second.bufferRange), "RenderGraph : Entry " << resTran.consumingOperation << "->" << resTran.consumingEntry << " overlaps with entry " << ect.operationName() << "->" << ect.entryName());
    }
  }

  // minimize the number of TransitionEntryID ( rteid ) ( the same rteid for the same entry ( operation, entry ) )
  struct RTEID
  {
    RTEID(const std::string& o, const std::string& e, uint32_t i)
      : op{ o }, en{ e }, id{ i }
    {
    }
    std::string op;
    std::string en;
    uint32_t    id;
  };
  std::vector<RTEID> genRteid, conRteid;
  uint32_t transitionID = generateTransitionID();
  for (auto& resTran : resTrans)
  {
    auto git = std::find_if(begin(genRteid), end(genRteid), [&resTran](const RTEID& rteid) { return rteid.op == resTran.generatingOperation && rteid.en == resTran.generatingEntry; });
    if (git == end(genRteid))
    {
      auto newRteid = generateTransitionEntryID();
      genRteid.push_back(RTEID(resTran.generatingOperation, resTran.generatingEntry, newRteid));
      auto genOp    = operations.find(resTran.generatingOperation);
      auto genEntry = genOp->second.entries.find(resTran.generatingEntry);
      transitions.push_back(ResourceTransition(newRteid, transitionID, genOp, genEntry, externalMemoryObjectName));
    }
    auto cit = std::find_if(begin(conRteid), end(conRteid), [&resTran](const RTEID& rteid) { return rteid.op == resTran.consumingOperation && rteid.en == resTran.consumingEntry; });
    if (cit == end(conRteid))
    {
      auto newRteid = generateTransitionEntryID();
      conRteid.push_back(RTEID(resTran.consumingOperation, resTran.consumingEntry, newRteid));
      auto conOp = operations.find(resTran.consumingOperation);
      auto conEntry = conOp->second.entries.find(resTran.consumingEntry);
      transitions.push_back(ResourceTransition(newRteid, transitionID, conOp, conEntry, externalMemoryObjectName));
    }
  }
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
  transitions.push_back(ResourceTransition(generateTransitionEntryID(), generateTransitionID(), op, entry, externalMemoryObjectName));
  valid = false;
}

void RenderGraph::addMissingResourceTransitions()
{
  for (auto opit = begin(operations); opit != end(operations); ++opit)
  {
    std::vector<ResourceTransition> emptyTransitions;
    auto opTransitions = getOperationIO(opit->first, opeAllInputsOutputs);
    for (auto opeit = begin(opit->second.entries); opeit != end(opit->second.entries); ++opeit)
      if (std::none_of(begin(opTransitions), end(opTransitions), [&opeit](const ResourceTransition& tr) { return opeit->first == tr.entryName(); }))
        emptyTransitions.push_back(ResourceTransition(generateTransitionEntryID(), generateTransitionID(), opit, opeit, std::string()));
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
    [transitionID, entryTypes](const ResourceTransition& c)->bool { return c.tid() == transitionID && (c.entry().entryType & entryTypes); });
  return results;
}

std::reference_wrapper<const ResourceTransition> RenderGraph::getTransition(uint32_t rteid) const
{
  auto it = std::find_if(begin(transitions), end(transitions), [rteid](const ResourceTransition& c)->bool { return c.rteid() == rteid; });
  CHECK_LOG_THROW(it == end(transitions), "Canot find transition rteid = " << rteid);
  return *it;
}


uint32_t RenderGraph::generateTransitionID()
{
  auto result = nextTransitionID++;
  return result;
}

uint32_t RenderGraph::generateTransitionEntryID()
{
  auto result = nextTransitionEntryID++;
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
    auto transitions = renderGraph.getTransitionIO(inTransition.get().tid(), opeAllOutputs);
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
    auto transitions = renderGraph.getTransitionIO(outTransition.get().tid(), opeAllInputs);
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
