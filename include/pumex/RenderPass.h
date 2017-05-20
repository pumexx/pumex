#pragma once
#include <unordered_map>
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

class Device;

struct PUMEX_EXPORT AttachmentDefinition
{
  enum Type { SwapChain, Depth, Color };
  AttachmentDefinition(Type type, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectMask, VkSampleCountFlagBits samples, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp, VkAttachmentLoadOp stencilLoadOp, VkAttachmentStoreOp stencilStoreOp, VkImageLayout initialLayout, VkImageLayout finalLayout, VkAttachmentDescriptionFlags flags);
  Type                         type;
  VkFormat                     format;
  VkImageUsageFlags            usage;
  VkImageAspectFlags           aspectMask;
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

  void         validate(std::shared_ptr<pumex::Device> device);
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