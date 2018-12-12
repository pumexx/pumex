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
  if (memoryObject->getType() == MemoryObject::moImage)
  {
    auto memoryImage = std::dynamic_pointer_cast<MemoryImage>(memoryObject);
    auto imageView = std::make_shared<ImageView>(memoryImage, memoryImage->getFullImageRange(), imageViewType);
    imageViews.insert({ name, imageView });
  }
}

RenderGraphImageInfo::RenderGraphImageInfo(const AttachmentDefinition& ad, const std::string& im, VkImageUsageFlags iu, bool iscim )
  : attachmentDefinition{ ad }, externalMemoryImageName{ im }, imageUsage{ iu }, isSwapchainImage { iscim }
{
}


RenderGraphExecutable::RenderGraphExecutable()
{
}

void RenderGraphExecutable::setExternalMemoryObjects(const RenderGraph& renderGraph, const ExternalMemoryObjects& memoryObjects)
{
  // copy RenderGraph associated resources to RenderGraphResults registered resources
  for (auto& mit : memoryObjects.memoryObjects)
  {
    std::set<uint32_t> visitedIDs;
    for (const auto& transition : renderGraph.transitions)
    {
      if (visitedIDs.find(transition.id()) != end(visitedIDs))
        continue;
      if (transition.externalMemoryObjectName() == mit.first)
      {
        visitedIDs.insert(transition.id());
        switch (mit.second->getType())
        {
        case MemoryObject::moBuffer:
        {
          memoryBuffers.insert({ transition.id(), std::dynamic_pointer_cast<MemoryBuffer>(mit.second) });
          break;
        }
        case MemoryObject::moImage:
        {
          memoryImages.insert({ transition.id(), std::dynamic_pointer_cast<MemoryImage>(mit.second) });
          auto vit = memoryObjects.imageViews.find(mit.first);
          if (vit != end(memoryObjects.imageViews))
            memoryImageViews.insert({ transition.id(), vit->second });
          break;
        }
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
  {
    for (const auto& command : commandSequence)
    {
      if (command->operation.name == operationName)
      {
        auto it = command->entries.find(entryName);
        if (it == end(command->entries))
          return std::shared_ptr<MemoryImage>();
        return getMemoryImage(it->second);
      }
    }
  }
  return std::shared_ptr<MemoryImage>();
}

std::shared_ptr<MemoryBuffer> RenderGraphExecutable::getMemoryBuffer(const std::string& operationName, const std::string entryName) const
{
  for (const auto& commandSequence : commands)
  {
    for (const auto& command : commandSequence)
    {
      if (command->operation.name == operationName)
      {
        auto it = command->entries.find(entryName);
        if (it == end(command->entries))
          return std::shared_ptr<MemoryBuffer>();
        return getMemoryBuffer(it->second);
      }
    }
  }
  return std::shared_ptr<MemoryBuffer>();
}

std::shared_ptr<ImageView> RenderGraphExecutable::getImageView(const std::string& operationName, const std::string entryName) const
{
  for (const auto& commandSequence : commands)
  {
    for (const auto& command : commandSequence)
    {
      if (command->operation.name == operationName)
      {
        auto it = command->entries.find(entryName);
        if (it == end(command->entries))
          return std::shared_ptr<ImageView>();
        return getImageView(it->second);
      }
    }
  }
  return std::shared_ptr<ImageView>();
}


std::shared_ptr<MemoryObject> RenderGraphExecutable::getMemoryObject(uint32_t transitionID) const
{
  auto ait = memoryObjectAliases.find(transitionID);
  if (ait == end(memoryObjectAliases))
    return std::shared_ptr<MemoryObject>();
  uint32_t aliasedID = ait->second;
  auto itImage = memoryImages.find(aliasedID);
  if (itImage == end(memoryImages))
  {
    auto itBuffer = memoryBuffers.find(aliasedID);
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

std::shared_ptr<ImageView> RenderGraphExecutable::getImageView(uint32_t transitionID) const
{
  auto ait = memoryObjectAliases.find(transitionID);
  if (ait == end(memoryObjectAliases))
    return std::shared_ptr<ImageView>();

  auto mit = memoryImageViews.find(ait->second);
  if (mit == end(memoryImageViews))
    return std::shared_ptr<ImageView>();
  return mit->second;
}

VkImageLayout RenderGraphExecutable::getImageLayout(const std::string& opName, uint32_t transitionID, int32_t indexAdd) const
{
  auto opit = operationIndices.find(opName);
  CHECK_LOG_THROW(opit == end(operationIndices), " Operation does not exist : " << opName);
  auto attachmentID = memoryObjectAliases.at(transitionID);
  return imageInfo.at(attachmentID).layouts.at(opit->second + indexAdd);
}

const std::vector<uint32_t>& RenderGraphExecutable::getOperationParticipants(uint32_t transitionID ) const
{
  auto attachmentID = memoryObjectAliases.at(transitionID);
  return imageInfo.at(attachmentID).operationParticipants;
}
