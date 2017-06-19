//
// Copyright(c) 2017 Pawe³ Ksiê¿opolski ( pumexx )
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
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

class Device;

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
  AttachmentReference( uint32_t attachment, VkImageLayout layout );

  uint32_t attachment;
  VkImageLayout layout;
  VkAttachmentReference getReference() const;
};

// struct storing information about subpass
struct PUMEX_EXPORT SubpassDefinition
{
  SubpassDefinition(VkPipelineBindPoint pipelineBindPoint, const std::vector<AttachmentReference>& inputAttachments, const std::vector<AttachmentReference>& colorAttachments, const std::vector<AttachmentReference>& resolveAttachments, const AttachmentReference& depthStencilAttachment, const std::vector<uint32_t>& preserveAttachments, VkSubpassDescriptionFlags flags = 0x0);

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

// class representing Vulkan render pass along with its attachments, subpasses and dependencies
class PUMEX_EXPORT RenderPass
{
public:
  RenderPass()                             = delete;
  explicit RenderPass(const std::vector<AttachmentDefinition>& attachments, const std::vector<SubpassDefinition>& subpasses, const std::vector<SubpassDependencyDefinition>& dependencies = std::vector<SubpassDependencyDefinition>());
  RenderPass(const RenderPass&)            = delete;
  RenderPass& operator=(const RenderPass&) = delete;
  ~RenderPass();

  void         validate(Device* device);
  VkRenderPass getHandle(VkDevice device) const;

  std::vector<AttachmentDefinition>        attachments;
  std::vector<SubpassDefinition>           subpasses;
  std::vector<SubpassDependencyDefinition> dependencies;
protected:
  struct PerDeviceData
  {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    bool         dirty      = true;
  };

  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};
	
}