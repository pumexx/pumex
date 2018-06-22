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
#include <pumex/PerObjectData.h>
#include <pumex/Command.h>
#include <pumex/MemoryObjectBarrier.h>

namespace pumex
{

class  Resource;
class  WorkflowResource;
class  RenderOperation;
class  RenderContextVisitor;
class  BuildCommandBufferVisitor;
class  FrameBuffer;

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
  SubpassDefinition(VkPipelineBindPoint pipelineBindPoint, const std::vector<AttachmentReference>& inputAttachments, const std::vector<AttachmentReference>& colorAttachments, const std::vector<AttachmentReference>& resolveAttachments, const AttachmentReference& depthStencilAttachment, const std::vector<uint32_t>& preserveAttachments, VkSubpassDescriptionFlags flags = 0x0, uint32_t multiViewMask = 0x0);
  SubpassDefinition& operator=(const SubpassDefinition& subpassDefinition);

  VkPipelineBindPoint                pipelineBindPoint;
  std::vector<VkAttachmentReference> inputAttachments;
  std::vector<VkAttachmentReference> colorAttachments;
  std::vector<VkAttachmentReference> resolveAttachments;
  VkAttachmentReference              depthStencilAttachment;
  std::vector<uint32_t>              preserveAttachments;
  VkSubpassDescriptionFlags          flags;

  uint32_t                           multiViewMask;

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
  RenderPass(RenderPass&&)                 = delete;
  RenderPass& operator=(RenderPass&&)      = delete;
  ~RenderPass();

  void         addSubPass(std::shared_ptr<RenderSubPass> renderSubPass);
  void         setRenderPassData(std::shared_ptr<FrameBuffer> frameBuffer, const std::vector<AttachmentDefinition>& attachments, const std::vector<VkClearValue>& clearValues);

  void         invalidate(const RenderContext& renderContext);
  void         validate(const RenderContext& renderContext);
  VkRenderPass getHandle(const RenderContext& renderContext) const;


  std::shared_ptr<FrameBuffer>              frameBuffer;
  std::vector<AttachmentDefinition>         attachments;
  std::vector<VkClearValue>                 clearValues;
  std::vector<char>                         clearValuesInitialized;
  std::vector<std::weak_ptr<RenderSubPass>> subPasses;
  std::vector<SubpassDependencyDefinition>  dependencies;
  bool                                      multiViewRenderPass = false;

protected:
  struct RenderPassInternal
  {
    RenderPassInternal()
      : renderPass{ VK_NULL_HANDLE }
    {}
    VkRenderPass renderPass;
  };
  typedef PerObjectData<RenderPassInternal, uint32_t> RenderPassData;

  mutable std::mutex                           mutex;
  std::unordered_map<VkDevice, RenderPassData> perObjectData;
  uint32_t                                     activeCount;
};

class RenderSubPass;
class ComputePass;

// really - I don't have idea how to name this crucial class :(
class PUMEX_EXPORT RenderCommand : public CommandBufferSource
{
public:
  enum CommandType { ctRenderSubPass, ctComputePass };
  RenderCommand(CommandType commandType);

  virtual void validate(const RenderContext& renderContext) = 0;
  virtual void applyRenderContextVisitor(RenderContextVisitor& visitor) = 0;
  virtual void buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor) = 0;

  virtual RenderSubPass* asRenderSubPass() = 0;
  virtual ComputePass*   asComputePass() = 0;

  CommandType commandType;
  std::shared_ptr<RenderOperation> operation;
  std::map<MemoryObjectBarrierGroup, std::vector<MemoryObjectBarrier>> barriersBeforeOp;
  std::map<MemoryObjectBarrierGroup, std::vector<MemoryObjectBarrier>> barriersAfterOp;
};


// class representing Vulkan graphics render pass along with its attachments, subpasses and dependencies
class PUMEX_EXPORT RenderSubPass : public RenderCommand
{
public:
  explicit RenderSubPass();

  void setSubPassDefinition(const SubpassDefinition& subPassDefinition);

  void validate(const RenderContext& renderContext) override;
  void applyRenderContextVisitor(RenderContextVisitor& visitor) override;
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

  void validate(const RenderContext& renderContext) override;
  void applyRenderContextVisitor(RenderContextVisitor& visitor) override;
  void buildCommandBuffer(BuildCommandBufferVisitor& commandVisitor) override;

  RenderSubPass* asRenderSubPass() override;
  ComputePass*   asComputePass() override;
};

}