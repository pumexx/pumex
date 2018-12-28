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
#include <pumex/RenderGraphExecution.h>
#include <pumex/RenderPass.h>
#include <pumex/utils/Log.h>

using namespace pumex;

void ExternalMemoryObjects::addMemoryObject(const std::string& name, const ResourceDefinition& resourceDefinition, std::shared_ptr<MemoryObject> memoryObject, VkImageViewType imageViewType)
{
  auto it = memoryObjects.find(name);
  CHECK_LOG_THROW(it != end(memoryObjects), "ExternalMemoryObjects : memory object with that name already defined : " << name);
  memoryObjects.insert({ name, memoryObject });
  resourceDefinitions.insert({ name,resourceDefinition });
}

RenderGraphImageInfo::RenderGraphImageInfo(const AttachmentDefinition& ad, const std::string& im, VkImageUsageFlags iu, VkImageCreateFlags ic, VkImageViewType ivt, bool iscim, VkImageLayout il)
  : attachmentDefinition{ ad }, externalMemoryImageName{ im }, imageUsage{ iu }, imageCreate{ ic }, imageViewType{ ivt }, isSwapchainImage{ iscim }, initialLayout{ il }
{
}

RenderGraphImageViewInfo::RenderGraphImageViewInfo(uint32_t ri, uint32_t ti, uint32_t oi, uint32_t op, const ImageSubresourceRange& ir)
  : rteid{ ri }, tid{ ti }, oid{ oi }, opidx {op}, imageRange{ ir }
{
}

RenderGraphBufferViewInfo::RenderGraphBufferViewInfo(uint32_t ri, uint32_t ti, uint32_t oi, uint32_t op, const BufferSubresourceRange& br)
: rteid{ ri }, tid{ ti }, oid{ oi }, opidx{ op }, bufferRange{ br }
{
}

RenderGraphExecutable::RenderGraphExecutable()
{
}

void RenderGraphExecutable::resizeImages(const RenderContext& renderContext, std::vector<std::shared_ptr<Image>>& swapChainImages)
{
  for (auto& memImage : memoryImages)
  {
    auto iiit = imageInfo.find(memImage.first);
    if (iiit == end(imageInfo))
      continue;
    if (!iiit->second.isSwapchainImage)
    {
      // if memory image uses the same trais on every object ( surface, device ) then it cannot change size for particular surface / device
      // Such image must have size set during creation ( look for MemoryImage constructors ) and it should not be defined as isSurfaceDependent
      if (memImage.second->usesSameTraitsPerObject())
        continue;
      const RenderGraphImageInfo& info = iiit->second;
      ImageSize imageSize = info.attachmentDefinition.attachmentSize;
      auto imageType      = vulkanImageTypeFromImageSize(imageSize);
      if (imageSize.type == isSurfaceDependent)
      {
        imageSize.type = isAbsolute;
        imageSize.size *= glm::vec3(renderContext.surface->swapChainSize.width, renderContext.surface->swapChainSize.height, 1);
      }
      ImageTraits imageTraits(info.attachmentDefinition.format, imageSize, info.imageUsage, false, info.initialLayout, info.imageCreate, imageType, VK_SHARING_MODE_EXCLUSIVE);
      memImage.second->setImageTraits(renderContext.surface, imageTraits);
    }
    else
      memImage.second->setImages(renderContext.surface, swapChainImages);
  }
}


void RenderGraphExecutable::setExternalMemoryObjects(const RenderGraph& renderGraph, const ExternalMemoryObjects& memoryObjects)
{
  // copy RenderGraph associated resources to RenderGraphResults registered resources
  for (auto& mit : memoryObjects.memoryObjects)
  {
    std::set<uint32_t> visitedIDs;
    for (const auto& transition : renderGraph.getTransitions())
    {
      if (visitedIDs.find(transition.tid()) != end(visitedIDs))
        continue;
      if (transition.externalMemoryObjectName() == mit.first)
      {
        visitedIDs.insert(transition.tid());
        switch (mit.second->getType())
        {
        case MemoryObject::moBuffer:
          memoryBuffers.insert({ transition.tid(), std::dynamic_pointer_cast<MemoryBuffer>(mit.second) });
          break;
        case MemoryObject::moImage:
          memoryImages.insert({ transition.tid(), std::dynamic_pointer_cast<MemoryImage>(mit.second) });
          break;
        default:
          break;
        }
      }
    }
  }
}

std::shared_ptr<MemoryImage> RenderGraphExecutable::getMemoryImage(const std::string& operationName, const std::string entryName) const
{
  for (const auto& commandSequence : commands)
    for (const auto& command : commandSequence)
      if (command->operation.name == operationName)
        return command->getImageViewByEntryName(entryName)->memoryImage;
  return std::shared_ptr<MemoryImage>();
}

std::shared_ptr<MemoryBuffer> RenderGraphExecutable::getMemoryBuffer(const std::string& operationName, const std::string entryName) const
{
  for (const auto& commandSequence : commands)
    for (const auto& command : commandSequence)
      if (command->operation.name == operationName)
      {
        auto it = command->entries.find(entryName);
        if (it == end(command->entries))
          return std::shared_ptr<MemoryBuffer>();
        uint32_t rteid = it->second;
        auto it2 = std::find_if(begin(bufferViewInfo), end(bufferViewInfo), [rteid](const RenderGraphBufferViewInfo& bfInfo) { return bfInfo.rteid == rteid; });
        if (it2 == end(bufferViewInfo))
          return std::shared_ptr<MemoryBuffer>();
        return getMemoryBuffer(it2->tid);
      }
  return std::shared_ptr<MemoryBuffer>();
}

std::shared_ptr<ImageView> RenderGraphExecutable::getImageView(const std::string& operationName, const std::string entryName) const
{
  for (const auto& commandSequence : commands)
    for (const auto& command : commandSequence)
      if (command->operation.name == operationName)
        return command->getImageViewByEntryName(entryName);
  return std::shared_ptr<ImageView>();
}

std::shared_ptr<BufferView> RenderGraphExecutable::getBufferView(const std::string& operationName, const std::string entryName) const
{
  for (const auto& commandSequence : commands)
    for (const auto& command : commandSequence)
      if (command->operation.name == operationName)
        return command->getBufferViewByEntryName(entryName);
  return std::shared_ptr<BufferView>();
}

std::shared_ptr<MemoryObject> RenderGraphExecutable::getMemoryObject(uint32_t transitionID) const
{
  auto ait = memoryObjectAliases.find(transitionID);
  if (ait == end(memoryObjectAliases))
    return std::shared_ptr<MemoryObject>();
  uint32_t objectID = ait->second;
  auto itImage = memoryImages.find(objectID);
  if (itImage == end(memoryImages))
  {
    auto itBuffer = memoryBuffers.find(objectID);
    if (itBuffer == end(memoryBuffers))
      return std::shared_ptr<MemoryObject>();
    return itBuffer->second;
  }
  return itImage->second;
}

std::shared_ptr<MemoryImage> RenderGraphExecutable::getMemoryImage(uint32_t transitionID) const
{
  auto ait = memoryObjectAliases.find(transitionID);
  if (ait == end(memoryObjectAliases))
    return std::shared_ptr<MemoryImage>();

  auto mit = memoryImages.find(ait->second);
  if (mit == end(memoryImages))
    return std::shared_ptr<MemoryImage>();
  return mit->second;
}

std::shared_ptr<MemoryBuffer> RenderGraphExecutable::getMemoryBuffer(uint32_t transitionID) const
{
  auto ait = memoryObjectAliases.find(transitionID);
  if (ait == end(memoryObjectAliases))
    return std::shared_ptr<MemoryBuffer>();

  auto mit = memoryBuffers.find(ait->second);
  if (mit == end(memoryBuffers))
    return std::shared_ptr<MemoryBuffer>();
  return mit->second;
}

VkImageLayout RenderGraphExecutable::getImageLayout(uint32_t opidx, uint32_t objectID, const ImageSubresourceRange& imageRange) const
{
  // find range opidx = <0,operationIndex> ( elements in imageViewInfo are sorted by opidx ) ( forward search )
  auto vit = std::find_if(begin(imageViewInfo), end(imageViewInfo), [opidx](const RenderGraphImageViewInfo& ivinfo) 
  { 
    return ivinfo.opidx > opidx; 
  });
  // find last operation that changed layout of this object in this imageRange ( reverse search )
  auto rvit = std::find_if(std::make_reverse_iterator(vit), rend(imageViewInfo), [objectID, &imageRange](const RenderGraphImageViewInfo& ivinfo) { return ivinfo.oid == objectID && (ivinfo.imageRange.contains(imageRange) || imageRange.contains(ivinfo.imageRange)) ; });
  // if not found - find first layout of objectID ( undefined for internal attachments or general for external attachments ) ( forward search )
  if (rvit == rend(imageViewInfo))
  {
    vit = std::find_if(begin(imageViewInfo), end(imageViewInfo), [objectID](const RenderGraphImageViewInfo& ivinfo) { return ivinfo.oid == objectID; });
    if (vit != end(imageViewInfo))
      return vit->layouts[0];
    // Really ?!? What abomination we are looking for ?
    return VK_IMAGE_LAYOUT_UNDEFINED;
  }
  return rvit->layouts[opidx];
}

VkImageLayout RenderGraphExecutable::getImageLayout(const std::string& opName, uint32_t objectID, const ImageSubresourceRange& imageRange, int32_t indexAdd) const
{
  auto opit = operationIndices.find(opName);
  CHECK_LOG_THROW(opit == end(operationIndices), " Operation does not exist : " << opName);
  uint32_t operationIndex = opit->second + indexAdd;
  return getImageLayout(operationIndex, objectID, imageRange);
}

std::vector<VkImageLayout> RenderGraphExecutable::getImageLayouts(uint32_t objectID, const ImageSubresourceRange& imageRange) const
{
  // all done in reverse search
  // find last use of object in that range - copy all layouts to results
  auto rvit = std::find_if(rbegin(imageViewInfo), rend(imageViewInfo), [objectID, &imageRange](const RenderGraphImageViewInfo& ivinfo) { return ivinfo.oid == objectID && (ivinfo.imageRange.contains(imageRange) || imageRange.contains(ivinfo.imageRange)); });
  // security - what if that objectID is not used in imageViewInfo at all ?
  std::vector<VkImageLayout> results(operationIndices.size() + 2, VK_IMAGE_LAYOUT_UNDEFINED);
  if (rvit != rend(imageViewInfo))
    results = rvit->layouts;
  while (rvit != rend(imageViewInfo))
  {
    rvit = std::find_if(rvit+1, rend(imageViewInfo), [objectID, &imageRange](const RenderGraphImageViewInfo& ivinfo) { return ivinfo.oid == objectID && (ivinfo.imageRange.contains(imageRange) || imageRange.contains(ivinfo.imageRange)); });
    if (rvit != rend(imageViewInfo))
      std::copy(begin(rvit->layouts), begin(rvit->layouts) + rvit->opidx + 1, begin(results));
  }
  return results;
}

std::vector<uint32_t> RenderGraphExecutable::getOperationParticipants(uint32_t objectID, const ImageSubresourceRange& imageRange) const
{
  // all done in reverse search
  // find last use of object in that range - copy all layouts to results
  auto rvit = std::find_if(rbegin(imageViewInfo), rend(imageViewInfo), [objectID, &imageRange](const RenderGraphImageViewInfo& ivinfo) { return ivinfo.oid == objectID && ivinfo.imageRange.contains(imageRange); });
  // security - what if that objectID is not used in imageViewInfo at all ?
  std::vector<uint32_t> results(operationIndices.size() + 2, 0);
  if (rvit != rend(imageViewInfo))
    results = rvit->operationParticipants;
  while (rvit != rend(imageViewInfo))
  {
    rvit = std::find_if(rvit + 1, rend(imageViewInfo), [objectID, &imageRange](const RenderGraphImageViewInfo& ivinfo) { return ivinfo.oid == objectID && ivinfo.imageRange.contains(imageRange); });
    if (rvit != rend(imageViewInfo))
      std::copy(begin(rvit->operationParticipants), begin(rvit->operationParticipants) + rvit->opidx + 1, begin(results));
  }
  return results;
}
