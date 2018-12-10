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

#pragma once
#include <pumex/Export.h>
#include <pumex/RenderGraph.h>
#include <pumex/MemoryBuffer.h>
#include <pumex/MemoryImage.h>

namespace pumex
{

class DeviceMemoryAllocator;
class RenderPass;
class RenderCommand;
class FrameBuffer;
class QueueTraits;

class PUMEX_EXPORT ExternalMemoryObjects
{
public:
  void addMemoryObject(const std::string& name, const ResourceDefinition& resourceDefinition, std::shared_ptr<MemoryObject> memoryObject, VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D);

  std::map<std::string, std::shared_ptr<MemoryObject>> memoryObjects;
  std::map<std::string, std::shared_ptr<ImageView>>    imageViews;
  std::map<std::string, ResourceDefinition>            resourceDefinitions;
};

class RenderGraphImageInfo
{
public:
  RenderGraphImageInfo() = default;
  RenderGraphImageInfo(const AttachmentDefinition& attachmentDefinition, const std::string& externalMemoryImageName, VkImageUsageFlags imageUsage, VkImageLayout layoutOutside, VkImageLayout initialLayout, VkImageLayout finalLayout, bool isSwapchainImage );

  AttachmentDefinition attachmentDefinition;
  std::string          externalMemoryImageName;
  VkImageUsageFlags    imageUsage       = 0;
  VkImageLayout        layoutOutside    = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageLayout        initialLayout    = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageLayout        finalLayout      = VK_IMAGE_LAYOUT_UNDEFINED;
  bool                 isSwapchainImage = false;
};

class PUMEX_EXPORT RenderGraphExecutable
{
public:
  RenderGraphExecutable();

  std::string                                              name;
  std::vector<QueueTraits>                                 queueTraits;
  std::vector<std::vector<std::shared_ptr<RenderCommand>>> commands;

  std::shared_ptr<DeviceMemoryAllocator>                   frameBufferAllocator;

  std::map<uint32_t, std::shared_ptr<MemoryImage>>         memoryImages;
  std::map<uint32_t, std::shared_ptr<ImageView>>           memoryImageViews;
  std::map<uint32_t, std::shared_ptr<MemoryBuffer>>        memoryBuffers;
  std::map<uint32_t, uint32_t>                             memoryObjectAliases;
  std::map<uint32_t, RenderGraphImageInfo>                 imageInfo;
  std::vector<std::shared_ptr<FrameBuffer>>                frameBuffers;

  void                           setExternalMemoryObjects(const RenderGraph& renderGraph, const ExternalMemoryObjects& memoryObjects);
  std::shared_ptr<MemoryImage>   getMemoryImage(const std::string& operationName, const std::string entryName) const;
  std::shared_ptr<MemoryBuffer>  getMemoryBuffer(const std::string& operationName, const std::string entryName) const;
  std::shared_ptr<ImageView>     getImageView(const std::string& operationName, const std::string entryName) const;

  std::shared_ptr<MemoryObject>  getMemoryObject(uint32_t transitionID) const;
  std::shared_ptr<MemoryImage>   getMemoryImage(uint32_t transitionID) const;
  std::shared_ptr<MemoryBuffer>  getMemoryBuffer(uint32_t transitionID) const;
  std::shared_ptr<ImageView>     getImageView(uint32_t transitionID) const;
};
	
}
