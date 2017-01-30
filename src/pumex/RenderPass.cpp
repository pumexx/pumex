#include <pumex/RenderPass.h>
#include <pumex/Device.h>
#include <pumex/utils/Log.h>

using namespace pumex;

AttachmentDefinition::AttachmentDefinition(VkFormat f, VkSampleCountFlagBits s, VkAttachmentLoadOp lop, VkAttachmentStoreOp sop, VkAttachmentLoadOp slop, VkAttachmentStoreOp ssop, VkImageLayout il, VkImageLayout fl, VkAttachmentDescriptionFlags fs)
  : format{ f }, samples{ s }, loadOp{ lop }, storeOp{ sop }, stencilLoadOp{ slop }, stencilStoreOp{ ssop }, initialLayout{ il }, finalLayout{ fl }, flags{fs}
{
}

VkAttachmentDescription AttachmentDefinition::getDescription() const
{
  VkAttachmentDescription desc{};
  desc.flags          = flags;
  desc.format         = format;
  desc.samples        = samples;
  desc.loadOp         = loadOp;
  desc.storeOp        = storeOp;
  desc.stencilLoadOp  = stencilLoadOp;
  desc.stencilStoreOp = stencilStoreOp;
  desc.initialLayout  = initialLayout;
  desc.finalLayout    = finalLayout;
  return desc;
}

AttachmentReference::AttachmentReference(uint32_t a, VkImageLayout l)
  : attachment{ a }, layout{ l }
{
}

VkAttachmentReference AttachmentReference::getReference() const
{
  VkAttachmentReference ref;
    ref.attachment = attachment;
    ref.layout     = layout;
  return ref;
}

SubpassDefinition::SubpassDefinition(VkPipelineBindPoint pbp, const std::vector<AttachmentReference>& ia, const std::vector<AttachmentReference>& ca, const std::vector<AttachmentReference>& ra, const AttachmentReference& da, const std::vector<uint32_t>& pa, VkSubpassDescriptionFlags fs)
  : pipelineBindPoint{ pbp }, preserveAttachments(pa), flags{ fs }
{
  for ( const auto& a : ia)
    inputAttachments.emplace_back(a.getReference());
  for (const auto& a : ca)
    colorAttachments.emplace_back(a.getReference());
  for (const auto& a : ra)
    resolveAttachments.emplace_back(a.getReference());
  depthStencilAttachment = da.getReference();
}

// Be advised : resulting description is as long valid as SubpassDefinition exists.
// We are passing pointers to internal elements here
VkSubpassDescription SubpassDefinition::getDescription() const
{
  VkSubpassDescription desc;
    desc.flags                   = flags;
    desc.pipelineBindPoint       = pipelineBindPoint;
    desc.inputAttachmentCount    = inputAttachments.size();
    desc.pInputAttachments       = inputAttachments.data();
    desc.colorAttachmentCount    = colorAttachments.size();
    desc.pColorAttachments       = colorAttachments.data();
    desc.pResolveAttachments     = resolveAttachments.data();
    desc.pDepthStencilAttachment = &depthStencilAttachment;
    desc.preserveAttachmentCount = preserveAttachments.size();
    desc.pPreserveAttachments    = preserveAttachments.data();
  return desc;
}

SubpassDependencyDefinition::SubpassDependencyDefinition(uint32_t ss, uint32_t ds, VkPipelineStageFlags ssm, VkPipelineStageFlags dsm, VkAccessFlags sam, VkAccessFlags dam, VkDependencyFlags df)
  : srcSubpass{ ss }, dstSubpass{ ds }, srcStageMask{ ssm }, dstStageMask{ dsm }, srcAccessMask{ sam }, dstAccessMask{ dam }, dependencyFlags{df}
{
}

VkSubpassDependency SubpassDependencyDefinition::getDependency() const
{
  VkSubpassDependency dep;
    dep.srcSubpass      = srcSubpass;
    dep.dstSubpass      = dstSubpass;
    dep.srcStageMask    = srcStageMask;
    dep.dstStageMask    = dstStageMask;
    dep.srcAccessMask   = srcAccessMask;
    dep.dstAccessMask   = dstAccessMask;
    dep.dependencyFlags = dependencyFlags;
  return dep;
}

RenderPass::RenderPass(const std::vector<AttachmentDefinition>& a, const std::vector<SubpassDefinition>& s, const std::vector<SubpassDependencyDefinition>& d)
  : attachments(a), subpasses(s), dependencies(d)
{
}

RenderPass::~RenderPass()
{
  for (auto& pddit : perDeviceData)
    vkDestroyRenderPass(pddit.first, pddit.second.renderPass, nullptr);
}

void RenderPass::validate(std::shared_ptr<pumex::Device> device)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (!pddit->second.dirty)
    return;
  if (pddit->second.renderPass != VK_NULL_HANDLE)
    vkDestroyRenderPass(pddit->first, pddit->second.renderPass, nullptr);

  std::vector<VkAttachmentDescription> attachmentDescriptions;
  for (const auto& ad : attachments)
    attachmentDescriptions.emplace_back(ad.getDescription());

  std::vector<VkSubpassDescription> subpassDescriptions;
  for (const auto& sp : subpasses)
    subpassDescriptions.emplace_back(sp.getDescription());

  std::vector<VkSubpassDependency> dependencyDescriptors;
  for (const auto& dp : dependencies)
    dependencyDescriptors.emplace_back(dp.getDependency());

  VkRenderPassCreateInfo renderPassCI = {};
    renderPassCI.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCI.attachmentCount = attachmentDescriptions.size();
    renderPassCI.pAttachments    = attachmentDescriptions.data();
    renderPassCI.subpassCount    = subpassDescriptions.size();
    renderPassCI.pSubpasses      = subpassDescriptions.data();
    renderPassCI.dependencyCount = dependencyDescriptors.size();
    renderPassCI.pDependencies   = dependencyDescriptors.data();
  VK_CHECK_LOG_THROW( vkCreateRenderPass(device->device, &renderPassCI, nullptr, &pddit->second.renderPass), "Could not create default render pass" );
  pddit->second.dirty = false;
}

VkRenderPass RenderPass::getHandle(VkDevice device) const
{
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.renderPass;
}
