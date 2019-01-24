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

void RenderOperation::addAttachmentInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageUsageFlags imageUsage, VkImageCreateFlags imageCreate)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment input is not an image : " << name << "->" << entryName);
  CHECK_LOG_THROW(!compareRenderOperationSizeWithImageSize(attachmentSize, resourceDefinition.attachment.attachmentSize, imageRange ), "RenderOperation : Attachment must have the same size as its operation : " << name << "->" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentInput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, imageUsage, imageCreate, VK_IMAGE_VIEW_TYPE_MAX_ENUM, std::string(), false } });
  //valid = false;
}

void RenderOperation::addAttachmentOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageUsageFlags imageUsage, VkImageCreateFlags imageCreate, bool storeAttachment)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment output is not an image : " << name << "->" << entryName);
  CHECK_LOG_THROW(!compareRenderOperationSizeWithImageSize(attachmentSize, resourceDefinition.attachment.attachmentSize, imageRange), "RenderOperation : Attachment must have the same size as its operation : " << name << "->" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentOutput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, imageUsage, imageCreate, VK_IMAGE_VIEW_TYPE_MAX_ENUM, std::string(), storeAttachment } });
  //valid = false;
}

void RenderOperation::addAttachmentResolveOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageUsageFlags imageUsage, VkImageCreateFlags imageCreate, bool storeAttachment, const std::string& sourceEntryName)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment output is not an image : " << name << "->" << entryName);
  // compare operation size and attachment size skipping samples
  CHECK_LOG_THROW(!compareRenderOperationSizeWithImageSize(attachmentSize, resourceDefinition.attachment.attachmentSize, imageRange), "RenderOperation : Attachment must have the same size as its operation : " << name << "->" << entryName);
  CHECK_LOG_THROW(sourceEntryName.empty(), "RenderOperation : Resolve source entry not defined : " << name << " : " << entryName);
  auto sourceEntry = entries.find(sourceEntryName);
  CHECK_LOG_THROW(sourceEntry == end(entries), "RenderOperation : Resolve source entry does not exist : " << name << "->" << entryName << "(" <<sourceEntryName << ")" );
  CHECK_LOG_THROW(sourceEntry->second.resourceDefinition.metaType != rmtImage, "RenderOperation : Resolve source entry is not an image : " << name << "->" << entryName << "(" << sourceEntryName << ")");

  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentResolveOutput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, imageUsage, imageCreate, VK_IMAGE_VIEW_TYPE_MAX_ENUM, sourceEntryName, storeAttachment } });
  //valid = false;
}

void RenderOperation::setAttachmentDepthInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageUsageFlags imageUsage, VkImageCreateFlags imageCreate)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment depth input is not an image : " << name << "->" << entryName);
  CHECK_LOG_THROW(!compareRenderOperationSizeWithImageSize(attachmentSize, resourceDefinition.attachment.attachmentSize, imageRange), "RenderOperation : Attachment must have the same size as its operation : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.attachment.attachmentType != atDepth &&
    resourceDefinition.attachment.attachmentType != atDepthStencil &&
    resourceDefinition.attachment.attachmentType != atStencil, "RenderOperation : Attachment type must be atDepth, atDepthStencil or atStencil : " << name << "->" << entryName);
  auto redundant = std::find_if(begin(entries), end(entries), [](std::pair<const std::string,RenderOperationEntry>& entry) { return (entry.second.entryType == opeAttachmentDepthInput || entry.second.entryType == opeAttachmentDepthOutput); });
  CHECK_LOG_THROW(redundant!= end(entries), "RenderOperation : There must be only one depth input or output : " << name << "->" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentDepthInput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, imageUsage, imageCreate, VK_IMAGE_VIEW_TYPE_MAX_ENUM, std::string(), false } });
  //valid = false;
}

void RenderOperation::setAttachmentDepthOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageUsageFlags imageUsage, VkImageCreateFlags imageCreate, bool storeAttachment)
{
  CHECK_LOG_THROW(entries.find(entryName) != end(entries), "RenderOperation : Entry with that name already defined : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.metaType != rmtImage, "RenderOperation : Resource used as attachment depth output is not an image : " << name << "->" << entryName);
  CHECK_LOG_THROW(!compareRenderOperationSizeWithImageSize(attachmentSize, resourceDefinition.attachment.attachmentSize, imageRange), "RenderOperation : Attachment must have the same size as its operation : " << name << "->" << entryName);
  CHECK_LOG_THROW(resourceDefinition.attachment.attachmentType != atDepth &&
    resourceDefinition.attachment.attachmentType != atDepthStencil &&
    resourceDefinition.attachment.attachmentType != atStencil, "RenderOperation : Attachmenmt type must be atDepth, atDepthStencil or atStencil : " << name << "->" << entryName);
  auto redundant = std::find_if(begin(entries), end(entries), [](std::pair<const std::string, RenderOperationEntry>& entry) { return (entry.second.entryType == opeAttachmentDepthInput || entry.second.entryType == opeAttachmentDepthOutput); });
  CHECK_LOG_THROW(redundant != end(entries), "RenderOperation : There must be only one depth input or output : " << name << "->" << entryName);
  entries.insert({ entryName, RenderOperationEntry{ opeAttachmentDepthOutput, resourceDefinition, loadOp, imageRange, VK_IMAGE_LAYOUT_UNDEFINED, imageUsage, imageCreate, VK_IMAGE_VIEW_TYPE_MAX_ENUM, std::string(), storeAttachment } });
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

ResourceTransition::ResourceTransition(uint32_t rteid, uint32_t tid, uint32_t oid, const std::list<RenderOperation>::const_iterator& op, const std::map<std::string, RenderOperationEntry>::const_iterator& e, const std::string& emon, VkImageLayout ela)
  : rteid_{ rteid }, tid_{ tid }, oid_{ oid }, operation_ { op }, entry_{ e }, externalMemoryObjectName_{ emon }, externalLayout_{ela}
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
  auto it = std::find_if(begin(operations), end(operations), [&op](const RenderOperation& opx) { return opx.name == op.name; });
  CHECK_LOG_THROW(it != end(operations), "RenderGraph : operation already exists : " + op.name);
  operations.push_back(op);
  valid = false;
}

uint32_t RenderGraph::addResourceTransition(const std::string& generatingOperation, const std::string& generatingEntry, const std::string& consumingOperation, const std::string& consumingEntry, uint32_t suggestedObjectID, const std::string& externalMemoryObjectName)
{
  return addResourceTransition({ generatingOperation, generatingEntry }, { { consumingOperation, consumingEntry } }, suggestedObjectID, externalMemoryObjectName);
}

uint32_t RenderGraph::addResourceTransition(const ResourceTransitionEntry& gen, const ResourceTransitionEntry& con, uint32_t suggestedObjectID, const std::string& externalMemoryObjectName)
{
  std::vector<ResourceTransitionEntry> cons;
  cons.push_back(con);
  return addResourceTransition( gen, cons, suggestedObjectID, externalMemoryObjectName );
}

uint32_t RenderGraph::addResourceTransition(const ResourceTransitionEntry& gen, const std::vector<ResourceTransitionEntry>& cons, uint32_t suggestedObjectID, const std::string& externalMemoryObjectName)
{
  CHECK_LOG_THROW(cons.empty(), "RenderGraph : vector of consumers is empty : " << gen.first);

  const auto genOp = std::find_if(begin(operations), end(operations), [&gen](const RenderOperation& opx) { return opx.name == gen.first; });
  CHECK_LOG_THROW(genOp == end(operations), "RenderGraph : generating operation not defined : " << gen.first);

  const auto genEntry = genOp->entries.find(gen.second);
  CHECK_LOG_THROW(genEntry == end(genOp->entries), "RenderGraph : operation " << gen.first << " does not have entry named : " << gen.second);
  CHECK_LOG_THROW((genEntry->second.entryType & opeAllOutputs) == 0, "RenderGraph : entry " << gen.first << "->" << gen.second << " is not an output");

  // generating entry may have zero or more outputs
  auto existingGenTransition = std::find_if(begin(transitions), end(transitions), [genOp, genEntry](const ResourceTransition& rt) { return (rt.operationIter() == genOp && rt.entryIter() == genEntry); });
  uint32_t transitionID, objectID;
  if (existingGenTransition != end(transitions))
  {
    // all transitions using the same output must have the same external resource defined
    CHECK_LOG_THROW(existingGenTransition->externalMemoryObjectName() != externalMemoryObjectName, "RenderGraph : All transitions using " << gen.first << "->" << gen.second << " must have the same external resource : " << existingGenTransition->externalMemoryObjectName() << " != " << externalMemoryObjectName);
    transitionID = existingGenTransition->tid();
    objectID     = existingGenTransition->oid();
    // if suggestedObjectID is provided, then it must be the same as the one that exists already - two different objects cannot use the same generating entry
    CHECK_LOG_THROW( suggestedObjectID!=0 && objectID != suggestedObjectID, "RenderGraph : All transitions using entry " << gen.first << "->" << gen.second << " must have the same objectID");
  }
  else
  {
    transitionID = generateTransitionID();
    objectID     = (suggestedObjectID!=0) ? suggestedObjectID : generateObjectID();
  }

  std::vector<ImageSubresourceRange>  imageRanges;
  std::set<VkImageLayout>             conImageLayouts;
  std::vector<BufferSubresourceRange> bufferRanges;
  for (const auto& con : cons)
  {
    CHECK_LOG_THROW(gen.first == con.first, "RenderGraph : generating and consuming operation can't be the same : " << gen.first);

    auto conOp = std::find_if(begin(operations), end(operations), [&con](const RenderOperation& opx) { return opx.name == con.first; });
    CHECK_LOG_THROW(conOp == end(operations), "RenderGraph : consuming operation not defined : " << con.first);

    auto conEntry = conOp->entries.find(con.second);
    CHECK_LOG_THROW(conEntry == end(conOp->entries), "RenderGraph : operation " << con.first << " does not have entry named : " << con.second);
    CHECK_LOG_THROW((conEntry->second.entryType & opeAllInputs) == 0, "RenderGraph : entry " << con.first << "->" << con.second << " is not an input");

    conImageLayouts.insert(conEntry->second.layout);

    // both entries must have the same resource definition
    CHECK_LOG_THROW(!(genEntry->second.resourceDefinition == conEntry->second.resourceDefinition), "RenderGraph : entries " << gen.first << "->" << gen.second << " and " << con.first << "->" << con.second << " must have the same resource definition");

    // only one consuming transition may exist
    auto existingConTransition = std::find_if(begin(transitions), end(transitions), [conOp, conEntry](const ResourceTransition& rt) { return (rt.operationIter() == conOp && rt.entryIter() == conEntry); });
    CHECK_LOG_THROW(existingConTransition != end(transitions), "RenderGraph : consuming operation may only have one entry : " << con.first << "->" << con.second);

    if ((conEntry->second.entryType & ( opeAllImages | opeAllAttachments )) != 0)
      imageRanges.push_back(conEntry->second.imageRange);
    else
      bufferRanges.push_back(conEntry->second.bufferRange);
  }
  CHECK_LOG_THROW(imageRanges.empty() == bufferRanges.empty(), "RenderGraph : all consuming operations must be either image based or buffer based");
  if (!imageRanges.empty())
  {
    auto consumentRange = mergeRanges(imageRanges);
    CHECK_LOG_THROW(consumentRange.valid() && !genEntry->second.imageRange.contains(consumentRange), "RenderGraph : generating transition image range must contain consuming image ranges" << gen.first << "->" << gen.second);
    CHECK_LOG_THROW(conImageLayouts.size() > 1, "RenderGraph : all consuming image layouts must be the same for generating tranistion : " << gen.first << "->" << gen.second);
  }
  else if (!bufferRanges.empty())
  {
    auto consumentRange = mergeRanges(bufferRanges);
    CHECK_LOG_THROW(consumentRange.valid() && !genEntry->second.bufferRange.contains(consumentRange), "RenderGraph : generating transition buffer range must contain consuming buffer ranges" << gen.first << "->" << gen.second);
  }

  if (existingGenTransition == end(transitions))
    transitions.push_back(ResourceTransition(generateTransitionEntryID(), transitionID, objectID, genOp, genEntry, externalMemoryObjectName, VK_IMAGE_LAYOUT_UNDEFINED));
  for (const auto& con : cons)
  {
    auto conOp = std::find_if(begin(operations), end(operations), [&con](const RenderOperation& opx) { return opx.name == con.first; });
    auto conEntry = conOp->entries.find(con.second);

    transitions.push_back(ResourceTransition(generateTransitionEntryID(), transitionID, objectID, conOp, conEntry, externalMemoryObjectName, VK_IMAGE_LAYOUT_UNDEFINED));
  }
  valid = false;
  return objectID;
}

uint32_t RenderGraph::addResourceTransition(const std::vector<ResourceTransitionEntry>& gens, const ResourceTransitionEntry& con, uint32_t suggestedObjectID, const std::string& externalMemoryObjectName)
{
  CHECK_LOG_THROW(gens.empty(), "RenderGraph : vector of generators is empty : " << con.first);
  auto conOp = std::find_if(begin(operations), end(operations), [&con](const RenderOperation& opx) { return opx.name == con.first; });
  CHECK_LOG_THROW(conOp == end(operations), "RenderGraph : consuming operation not defined : " << con.first);

  auto conEntry = conOp->entries.find(con.second);
  CHECK_LOG_THROW(conEntry == end(conOp->entries), "RenderGraph : operation " << con.first << " does not have entry named : " << con.second);
  CHECK_LOG_THROW((conEntry->second.entryType & opeAllInputs) == 0, "RenderGraph : entry " << con.first << "->" << con.second << " is not an input");

  // consuming entry may have inly one input
  auto existingConTransition = std::find_if(begin(transitions), end(transitions), [conOp, conEntry](const ResourceTransition& rt) { return (rt.operationIter() == conOp && rt.entryIter() == conEntry); });
  CHECK_LOG_THROW(existingConTransition != end(transitions), "RenderGraph : consuming operation may only have one entry : " << con.first << "->" << con.second);

  // Before we add any transition - we must choose tid and oid for new set of transitions. Problem is that some generating transitions may be defined already.
  // OK, so idea is that all existing generating transitions must have at most one tid and oid defined.
  std::set<uint32_t>                  existingTransitionID;
  std::set<uint32_t>                  existingObjectID;
  std::vector<ImageSubresourceRange>  imageRanges;
  std::set<VkImageLayout>             genImageLayouts;
  std::vector<BufferSubresourceRange> bufferRanges;

  for (const auto& gen : gens)
  {
    auto genOp = std::find_if(begin(operations), end(operations), [&gen](const RenderOperation& opx) { return opx.name == gen.first; });
    CHECK_LOG_THROW(genOp == end(operations), "RenderGraph : generating operation not defined : " << gen.first);

    auto genEntry = genOp->entries.find(gen.second);
    CHECK_LOG_THROW(genEntry == end(genOp->entries), "RenderGraph : operation " << gen.first << " does not have entry named : " << gen.second);
    CHECK_LOG_THROW((genEntry->second.entryType & opeAllOutputs) == 0, "RenderGraph : entry " << gen.first << "->" << gen.second << " is not an output");

    // both entries must have the same resource definition
    CHECK_LOG_THROW(!(genEntry->second.resourceDefinition == conEntry->second.resourceDefinition), "RenderGraph : entries " << gen.first << "->" << gen.second << " and " << con.first << "->" << con.second << " must have the same resource definition");

    genImageLayouts.insert(genEntry->second.layout);

    auto existingGenTransition = std::find_if(begin(transitions), end(transitions), [genOp, genEntry](const ResourceTransition& rt) { return (rt.operationIter() == genOp && rt.entryIter() == genEntry); });
    if (existingGenTransition != end(transitions))
    {
      // all transitions using the same output must have the same external resource defined
      CHECK_LOG_THROW(existingGenTransition->externalMemoryObjectName() != externalMemoryObjectName, "RenderGraph : All transitions using " << gen.first << "->" << gen.second << " must have the same external resource : " << existingGenTransition->externalMemoryObjectName() << " != " << externalMemoryObjectName);

      existingTransitionID.insert(existingGenTransition->tid());
      existingObjectID.insert(existingGenTransition->oid());
    }
    if ((genEntry->second.entryType & ( opeAllImages | opeAllAttachments )) != 0)
      imageRanges.push_back(genEntry->second.imageRange);
    else
      bufferRanges.push_back(genEntry->second.bufferRange);
  }
  CHECK_LOG_THROW(imageRanges.empty() == bufferRanges.empty(), "RenderGraph : all generating operations must be either image based or buffer based");
  if (!imageRanges.empty())
  {
    auto generatorRange = mergeRanges(imageRanges);
    CHECK_LOG_THROW(generatorRange.valid() && !generatorRange.contains(conEntry->second.imageRange), "RenderGraph : generating transition image range must contain consuming image ranges" << con.first << "->" << con.second);
    CHECK_LOG_THROW(genImageLayouts.size() > 1, "RenderGraph : all generating image layouts must be the same for consuming tranistion : " << con.first << "->" << con.second);
    CHECK_LOG_THROW(anyRangeOverlaps(imageRanges), "RenderGraph : all generating image transitions must have disjunctive image ranges : " << con.first << "->" << con.second);
  }
  else if( !bufferRanges.empty() )
  {
    auto generatorRange = mergeRanges(bufferRanges);
    CHECK_LOG_THROW(generatorRange.valid() && !generatorRange.contains( conEntry->second.bufferRange), "RenderGraph : generating transition buffer range must contain consuming buffer ranges" << con.first << "->" << con.second);
    CHECK_LOG_THROW(anyRangeOverlaps(bufferRanges), "RenderGraph : all generating buffer transitions must have disjunctive buffer ranges : " << con.first << "->" << con.second);
  }

  uint32_t transitionID = generateTransitionID();
  uint32_t objectID;
  CHECK_LOG_THROW(existingTransitionID.size() > 1, "RenderGraph : cannot add generating transitions, because some transitions already exist and have different IDs. Consumer : " << con.first << "->" << con.second);
  if (!existingTransitionID.empty())
  {
    transitionID = *begin(existingTransitionID);
    objectID     = *begin(existingObjectID);
    // if suggestedObjectID is provided, then it must be the same as the one that exists already - two different objects cannot use the same generating entry
    CHECK_LOG_THROW(suggestedObjectID != 0 && objectID != suggestedObjectID, "RenderGraph : All transitions using consuming entry " << con.first << "->" << con.second << " must have the same objectID");
  }
  else
  {
    transitionID = generateTransitionID();
    objectID     = (suggestedObjectID != 0) ? suggestedObjectID : generateObjectID();
  }

  transitions.push_back(ResourceTransition(generateTransitionEntryID(), transitionID, objectID, conOp, conEntry, externalMemoryObjectName, VK_IMAGE_LAYOUT_UNDEFINED));
  for (const auto& gen : gens)
  {
    auto genOp = std::find_if(begin(operations), end(operations), [&gen](const RenderOperation& opx) { return opx.name == gen.first; });
    auto genEntry = genOp->entries.find(gen.second);

    auto existingGenTransition = std::find_if(begin(transitions), end(transitions), [genOp, genEntry](const ResourceTransition& rt) { return (rt.operationIter() == genOp && rt.entryIter() == genEntry); });
    if (existingGenTransition == end(transitions))
      transitions.push_back(ResourceTransition(generateTransitionEntryID(), transitionID, objectID, genOp, genEntry, externalMemoryObjectName, VK_IMAGE_LAYOUT_UNDEFINED));
  }
  valid = false;
  return objectID;
}

uint32_t RenderGraph::addResourceTransition(const std::string& opName, const std::string& entryName, uint32_t suggestedObjectID, const std::string& externalMemoryObjectName, VkImageLayout externalLayout)
{
  return addResourceTransition({ opName, entryName }, suggestedObjectID, externalMemoryObjectName, externalLayout);
}

uint32_t RenderGraph::addResourceTransition(const ResourceTransitionEntry& tren, uint32_t suggestedObjectID, const std::string& externalMemoryObjectName, VkImageLayout externalLayout)
{
  auto op = std::find_if(begin(operations), end(operations), [&tren](const RenderOperation& opx) { return opx.name == tren.first; });
  CHECK_LOG_THROW(op == end(operations), "RenderGraph : generating operation not defined : " << tren.first);

  auto entry = op->entries.find(tren.second);
  CHECK_LOG_THROW(entry == end(op->entries), "RenderGraph : operation " << tren.first << " does not have entry named : " << tren.second);

  auto existingTransition = std::find_if(begin(transitions), end(transitions), [op, entry](const ResourceTransition& rt) { return (rt.operationIter() == op && rt.entryIter() == entry); });
  uint32_t transitionID, objectID;
  if (existingTransition != end(transitions))
  {
    // all transitions using the same output must have the same external resource defined
    CHECK_LOG_THROW(existingTransition->externalMemoryObjectName() != externalMemoryObjectName, "RenderGraph : All transitions using " << tren.first << "->" << tren.second << " must have the same external resource : " << existingTransition->externalMemoryObjectName() << " != " << externalMemoryObjectName);
    transitionID = existingTransition->tid();
    objectID     = existingTransition->oid();
    // if suggestedObjectID is provided, then it must be the same as the one that exists already - two different objects () cannot use the same generating entry
    CHECK_LOG_THROW(suggestedObjectID != 0 && objectID != suggestedObjectID, "RenderGraph : All transitions using entry " << tren.first << "->" << tren.second << " must have the same objectID");
  }
  else
  {
    transitionID = generateTransitionID();
    objectID     = (suggestedObjectID != 0) ? suggestedObjectID : generateObjectID();
  }
  transitions.push_back(ResourceTransition(generateTransitionEntryID(), transitionID, objectID, op, entry, externalMemoryObjectName, externalLayout));
  valid = false;
  return objectID;
}

void RenderGraph::addMissingResourceTransitions()
{
  for (auto opit = begin(operations); opit != end(operations); ++opit)
  {
    std::vector<ResourceTransition> emptyTransitions;
    auto opTransitions = getOperationIO(opit->name, opeAllInputsOutputs);
    for (auto opeit = begin(opit->entries); opeit != end(opit->entries); ++opeit)
      if (std::none_of(begin(opTransitions), end(opTransitions), [&opeit](const ResourceTransition& tr) { return opeit->first == tr.entryName(); }))
        emptyTransitions.push_back(ResourceTransition(generateTransitionEntryID(), generateTransitionID(), generateObjectID(), opit, opeit, std::string(), VK_IMAGE_LAYOUT_UNDEFINED));
    std::copy(begin(emptyTransitions), end(emptyTransitions), std::back_inserter(transitions));
  }
}

std::vector<std::string> RenderGraph::getRenderOperationNames() const
{
  std::vector<std::string> results;
  for (auto& op : operations)
    results.push_back(op.name);
  return results;
}

const RenderOperation& RenderGraph::getRenderOperation(const std::string& opName) const
{
  auto it = std::find_if(begin(operations), end(operations), [&opName](const RenderOperation& opx) { return opx.name == opName; });
  CHECK_LOG_THROW(it == end(operations), "RenderGraph : there is no operation with name " + opName);
  return *it;
}

RenderOperation& RenderGraph::getRenderOperation(const std::string& opName)
{
  auto it = std::find_if(begin(operations), end(operations), [&opName](const RenderOperation& opx) { return opx.name == opName; });
  CHECK_LOG_THROW(it == end(operations), "RenderGraph : there is no operation with name " + opName);
  return *it;
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

std::vector<std::reference_wrapper<const ResourceTransition>> RenderGraph::getObjectIO(uint32_t objectID, OperationEntryTypeFlags entryTypes) const
{
  std::vector<std::reference_wrapper<const ResourceTransition>> results;
  std::copy_if(begin(transitions), end(transitions), std::back_inserter(results),
    [objectID, entryTypes](const ResourceTransition& c)->bool { return c.oid() == objectID && (c.entry().entryType & entryTypes); });
  return results;
}

uint32_t RenderGraph::generateTransitionEntryID()
{
  auto result = nextTransitionEntryID++;
  return result;
}

uint32_t RenderGraph::generateTransitionID()
{
  auto result = nextTransitionID++;
  return result;
}

uint32_t RenderGraph::generateObjectID()
{
  auto result = nextObjectID++;
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
  // operation is initial when there are no input resources, or input resources do not have outputs that created them
  RenderOperationSet initialOperations;
  for (const auto& it : renderGraph.getOperations())
  {
    auto inTransitions = renderGraph.getOperationIO(it.name, opeAllInputs);
    if(inTransitions.empty())
      initialOperations.insert(it);
    bool existingOutputs = false;
    for (auto& inTransition : inTransitions)
    {
      auto outTransitions = renderGraph.getTransitionIO(inTransition.get().tid(), opeAllOutputs);
      existingOutputs = !outTransitions.empty();
      if(existingOutputs)
        break;
    }
    if(!existingOutputs)
      initialOperations.insert(it);
  }
  return initialOperations;
}

RenderOperationSet getFinalOperations(const RenderGraph& renderGraph)
{
  // operation is final when there are no output resources, or output resources do not have inputs that consume them
  RenderOperationSet finalOperations;
  for (const auto& it : renderGraph.getOperations())
  {
    auto outTransitions = renderGraph.getOperationIO(it.name, opeAllOutputs);
    if (outTransitions.empty())
      finalOperations.insert(it);
    bool existingInputs = false;
    for (auto& outTransition : outTransitions)
    {
      auto inTransitions = renderGraph.getTransitionIO(outTransition.get().tid(), opeAllInputs);
      existingInputs = !inTransitions.empty();
      if (existingInputs)
        break;
    }
    if (!existingInputs)
      finalOperations.insert(it);
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
