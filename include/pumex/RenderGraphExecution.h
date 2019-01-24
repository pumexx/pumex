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
  std::map<std::string, ResourceDefinition>            resourceDefinitions;
};

class RenderGraphImageInfo
{
public:
  RenderGraphImageInfo() = default;
  RenderGraphImageInfo(const AttachmentDefinition& attachmentDefinition, const std::string& externalMemoryImageName, VkImageUsageFlags imageUsage, VkImageCreateFlags imageCreate, bool isSwapchainImage, VkImageLayout initialLayout);

  AttachmentDefinition       attachmentDefinition;
  std::string                externalMemoryImageName;
  VkImageUsageFlags          imageUsage       = 0;
  VkImageCreateFlags         imageCreate      = 0;
  bool                       isSwapchainImage = false;
  VkImageLayout              initialLayout      = VK_IMAGE_LAYOUT_UNDEFINED;
};

class RenderGraphImageViewInfo
{
public:
  RenderGraphImageViewInfo() = default;
  RenderGraphImageViewInfo(uint32_t tid, uint32_t oid, uint32_t opidx, std::shared_ptr<ImageView> imageView);
  uint32_t                   tid;   // ResourceTransition.id
  uint32_t                   oid;   // object id ( after image aliasing )
  uint32_t                   opidx; // operation index
  std::shared_ptr<ImageView> imageView;
  std::vector<VkImageLayout> layouts;
  std::vector<uint32_t>      operationParticipants;
};

class RenderGraphBufferViewInfo
{
public:
  RenderGraphBufferViewInfo() = default;
  RenderGraphBufferViewInfo(uint32_t tid, uint32_t oid, uint32_t opidx, const BufferSubresourceRange& bufferRange);
  uint32_t                   tid;   // ResourceTransition.id
  uint32_t                   oid;   // object id ( after image aliasing )
  uint32_t                   opidx; // operation index
  BufferSubresourceRange     bufferRange;
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
  std::map<uint32_t, std::shared_ptr<MemoryBuffer>>        memoryBuffers;
  std::vector<std::shared_ptr<FrameBuffer>>                frameBuffers;

  std::map<std::string, uint32_t>                          operationIndices; // works on image imageInfo.layouts and imageViewInfo.operationParticipants

  std::map<uint32_t, uint32_t>                             memoryObjectAliases;
  std::map<uint32_t, RenderGraphImageInfo>                 imageInfo;

  std::vector<RenderGraphImageViewInfo>                    imageViewInfo;
  std::map<uint32_t, std::size_t>                          imageViewInfoByRteID;
  std::vector<RenderGraphBufferViewInfo>                   bufferViewInfo;
  std::map<uint32_t, std::size_t>                          bufferViewInfoByRteID;

  void resizeImages(const RenderContext& renderContext, std::vector<std::shared_ptr<Image>>& swapChainImages);

  void                           setExternalMemoryObjects(const RenderGraph& renderGraph, const ExternalMemoryObjects& memoryObjects);
  std::shared_ptr<MemoryImage>   getMemoryImage(const std::string& operationName, const std::string entryName) const;
  std::shared_ptr<MemoryBuffer>  getMemoryBuffer(const std::string& operationName, const std::string entryName) const;
  std::shared_ptr<ImageView>     getImageView(const std::string& operationName, const std::string entryName) const;
  std::shared_ptr<BufferView>    getBufferView(const std::string& operationName, const std::string entryName) const;


  std::shared_ptr<MemoryObject>  getMemoryObject(uint32_t objectID) const;
  std::shared_ptr<MemoryImage>   getMemoryImage(uint32_t  objectID) const;
  std::shared_ptr<MemoryBuffer>  getMemoryBuffer(uint32_t objectID) const;

  VkImageLayout                  getImageLayout(uint32_t opidx, uint32_t objectID, const ImageSubresourceRange& imageRange) const;
  VkImageLayout                  getImageLayout(const std::string& opName, uint32_t objectID, const ImageSubresourceRange& imageRange, int32_t indexAdd) const;
  std::vector<VkImageLayout>     getImageLayouts(uint32_t objectID, const ImageSubresourceRange& imageRange) const;
  std::vector<uint32_t>          getOperationParticipants(uint32_t objectID, const ImageSubresourceRange& imageRange) const;
};
	
}
