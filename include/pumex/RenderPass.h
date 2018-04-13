//
// Copyright(c) 2017-2018 Pawe³ Ksiê¿opolski ( pumexx )
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
#include <unordered_map>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/Command.h>

namespace pumex
{

class  Resource;
class  WorkflowResource;
class  RenderOperation;
class  ValidateGPUVisitor;
class  BuildCommandBufferVisitor;
struct FrameBufferImageDefinition;

// VkAttachmentDescription wrapper
struct PUMEX_EXPORT AttachmentDefinition
{
  AttachmentDefinition(uint32_t imageDefinitionIndex, VkFormat format, VkSampleCountFlagBits samples, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp, VkAttachmentLoadOp stencilLoadOp, VkAttachmentStoreOp stencilStoreOp, VkImageLayout initialLayout, VkImageLayout finalLayout, VkAttachmentDescriptionFlags flags);
  uint32_t                     imageDefinitionIndex;
  VkFormat                     format;
  VkSampleCountFlagBits        samples;
  VkAttachmentLoadOp           loadOp;
  VkAttachmentStoreOp          storeOp;
  VkAttachmentLoadOp           stencilLoadOp;
  VkAttachmentStoreOp          stencilStoreOp;
  VkImageLayout                initialLayout;
  VkImageLayout                finalLayout;
  VkAttachmentDescriptionFlags flags;

  VkAttachmentDescription getDescription() const;
};

struct PUMEX_EXPORT AttachmentReference
{
  AttachmentReference();
  AttachmentReference( uint32_t attachment, VkImageLayout layout );

  uint32_t attachment;
  VkImageLayout layout;
  VkAttachmentReference getReference() const;
};

// struct storing information about subpass
struct PUMEX_EXPORT SubpassDefinition
{
  SubpassDefinition();
  SubpassDefinition(VkPipelineBindPoint pipelineBindPoint, const std::vector<AttachmentReference>& inputAttachments, const std::vector<AttachmentReference>& colorAttachments, const std::vector<AttachmentReference>& resolveAttachments, const AttachmentReference& depthStencilAttachment, const std::vector<uint32_t>& preserveAttachments, VkSubpassDescriptionFlags flags = 0x0);
  SubpassDefinition& operator=(const SubpassDefinition& subpassDefinition);

  VkPipelineBindPoint                pipelineBindPoint;
  std::vector<VkAttachmentReference> inputAttachments;
  std::vector<VkAttachmentReference> colorAttachments;
  std::vector<VkAttachmentReference> resolveAttachments;
  VkAttachmentReference              depthStencilAttachment;
  std::vector<uint32_t>              preserveAttachments;
  VkSubpassDescriptionFlags          flags;

  VkSubpassDescription getDescription() const;
};

// struct storing information about subpass dependencies
struct PUMEX_EXPORT SubpassDependencyDefinition
{
  SubpassDependencyDefinition( uint32_t srcSubpass, uint32_t dstSubpass, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkDependencyFlags dependencyFlags = 0 );

  uint32_t                srcSubpass;
  uint32_t                dstSubpass;
  VkPipelineStageFlags    srcStageMask;
  VkPipelineStageFlags    dstStageMask;
  VkAccessFlags           srcAccessMask;
  VkAccessFlags           dstAccessMask;
  VkDependencyFlags       dependencyFlags;

  VkSubpassDependency getDependency() const;
};

class RenderSubPass;

// class representing Vulkan graphics render pass along with its attachments, subpasses and dependencies
class PUMEX_EXPORT RenderPass : public std::enable_shared_from_this<RenderPass>
{
public:
  explicit RenderPass();
  RenderPass(const RenderPass&)            = delete;
  RenderPass& operator=(const RenderPass&) = delete;
  ~RenderPass();

  void initializeAttachments(const std::vector<FrameBufferImageDefinition>& frameBufferDefinitions, const std::unordered_map<std::string, uint32_t>& attachmentIndex, std::vector<VkImageLayout>& lastLayout);
  void addSubPass(std::shared_ptr<RenderSubPass> renderSubPass);
  void updateAttachments(std::shared_ptr<RenderSubPass> renderSubPass, std::vector<FrameBufferImageDefinition>& frameBufferDefinitions, const std::unordered_map<std::string, uint32_t>& attachmentIndex, std::vector<VkImageLayout>& lastLayout);

  void         validate(const RenderContext& renderContext);
  VkRenderPass getHandle(VkDevice device) const;

  std::vector<AttachmentDefinition>         attachments;
  std::vector<VkClearValue>                 clearValues;
  std::vector<bool>                         clearValuesInitialized;
  std::vector<std::weak_ptr<RenderSubPass>> subPasses;
  std::vector<SubpassDependencyDefinition>  dependencies;

protected:
  struct PerDeviceData
  {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    bool         valid      = false;
  };

  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

class PUMEX_EXPORT ResourceBarrier
{
public:
  ResourceBarrier(std::shared_ptr<Resource> resource, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex, VkImageLayout oldLayout, VkImageLayout newLayout);

  std::shared_ptr<Resource> resource;
  VkAccessFlags             srcAccessMask;
  VkAccessFlags             dstAccessMask;
  uint32_t                  srcQueueFamilyIndex;
  uint32_t                  dstQueueFamilyIndex;
  VkImageLayout             oldLayout;
  VkImageLayout             newLayout;

};

class PUMEX_EXPORT ResourceBarrierGroup
{
public:
  ResourceBarrierGroup(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags);

  VkPipelineStageFlags                 srcStageMask;
  VkPipelineStageFlags                 dstStageMask;
  VkDependencyFlags                    dependencyFlags;
};

inline bool operator<(const ResourceBarrierGroup& lhs, const ResourceBarrierGroup& rhs);

class RenderSubPass;
class ComputePass;

// really - I don't have idea how to name this crucial class :(
class PUMEX_EXPORT RenderCommand : public CommandBufferSource
{
public:
  enum CommandType { ctRenderSubPass, ctComputePass };
  RenderCommand(CommandType commandType);

  virtual void validateGPUData(ValidateGPUVisitor& updateVisitor) = 0;
  virtual void buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor) = 0;

  virtual RenderSubPass* asRenderSubPass() = 0;
  virtual ComputePass*   asComputePass() = 0;

  CommandType commandType;
  std::shared_ptr<RenderOperation> operation;
  std::map<ResourceBarrierGroup, std::vector<ResourceBarrier>> barriersBeforeOp;
  std::map<ResourceBarrierGroup, std::vector<ResourceBarrier>> barriersAfterOp;
};


// class representing Vulkan graphics render pass along with its attachments, subpasses and dependencies
class PUMEX_EXPORT RenderSubPass : public RenderCommand
{
public:
  explicit RenderSubPass();

  void buildSubPassDefinition(const std::unordered_map<std::string, uint32_t>& attachmentIndex);

  void validateGPUData(ValidateGPUVisitor& updateVisitor) override;
  void buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor) override;

  RenderSubPass* asRenderSubPass() override;
  ComputePass*   asComputePass() override;


  std::shared_ptr<RenderPass>      renderPass;
  uint32_t                         subpassIndex;

  SubpassDefinition                definition;
};

class PUMEX_EXPORT ComputePass : public RenderCommand
{
public:
  explicit ComputePass();

  void validateGPUData(ValidateGPUVisitor& updateVisitor) override;
  void buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor) override;

  RenderSubPass* asRenderSubPass() override;
  ComputePass*   asComputePass() override;
};

bool operator<(const ResourceBarrierGroup& lhs, const ResourceBarrierGroup& rhs)
{
  if (lhs.srcStageMask != rhs.srcStageMask)
    return lhs.srcStageMask < rhs.srcStageMask;
  if (lhs.dstStageMask != rhs.dstStageMask)
    return lhs.dstStageMask < rhs.dstStageMask;
  return lhs.dependencyFlags < rhs.dependencyFlags;
}


}