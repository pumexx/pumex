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
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/Command.h>

namespace pumex
{

class Device;

struct PUMEX_EXPORT LoadOp
{
  enum Type { Load, Clear, DontCare };
  LoadOp() = delete;
  LoadOp(Type lType, const glm::vec4& color)
    : loadType{ lType }, clearColor{ color }
  {
  }

  Type loadType;
  glm::vec4 clearColor;
};

inline LoadOp loadOpLoad();
inline LoadOp loadOpClear(const glm::vec4& color = glm::vec4(0.0f));
inline LoadOp loadOpDontCare();

struct PUMEX_EXPORT StoreOp
{
  enum Type { Store, DontCare };
  StoreOp() = delete;
  StoreOp(Type sType)
    : storeType{ sType }
  {
  }
  Type storeType;
};

inline StoreOp storeOpStore();
inline StoreOp storeOpDontCare();

class PUMEX_EXPORT RenderOperationAttachment
{
public:
  RenderOperationAttachment();
  RenderOperationAttachment(const std::string& name, VkImageLayout operationLayout);
  RenderOperationAttachment(const std::string& name, VkImageLayout operationLayout, LoadOp loadOperation);

  std::string   name;
  VkImageLayout operationLayout;
  LoadOp        loadOperation;
};

class PUMEX_EXPORT RenderOperation
{
public:
  enum Type { Graphics, Compute };
  RenderOperation();
  RenderOperation(const std::string& name, Type operationType);

  void addAttachmentInput(const RenderOperationAttachment& roAttachment);
  void addAttachmentOutput(const RenderOperationAttachment& attachmentConfig);
  void addResolveOutput(const RenderOperationAttachment& attachmentConfig);
  void setDepthOutput(const RenderOperationAttachment& attachmentConfig);

  std::string                                                name;
  Type                                                       operationType;
  std::unordered_map<std::string, RenderOperationAttachment> inputAttachments;
  std::unordered_map<std::string, RenderOperationAttachment> outputAttachments;
  std::unordered_map<std::string, RenderOperationAttachment> resolveAttachments;
  RenderOperationAttachment                                  depthAttachment;
  bool                                                       enabled;
};

enum AttachmentType { atSurface, atColor, atDepth };
enum AttachmentSize { asAbsolute, asSurfaceDependent };

class PUMEX_EXPORT RenderWorkflowAttachment
{
public:
  RenderWorkflowAttachment() = default;
  RenderWorkflowAttachment(const std::string& name, AttachmentType attachmentType, VkFormat format, VkSampleCountFlagBits samples, AttachmentSize sizeType, glm::vec2 imageSize, bool persistent);

  std::string           name;
  AttachmentType        attachmentType;
  VkFormat              format;
  VkSampleCountFlagBits samples;
  AttachmentSize        sizeType;
  glm::vec2             imageSize;
  bool                  persistent;
};

class PUMEX_EXPORT RenderWorkflow
{
public:
  RenderWorkflow()                                 = delete;
  explicit RenderWorkflow(const std::string& name, const std::vector<RenderWorkflowAttachment>& attachments);
  RenderWorkflow(const RenderWorkflow&)            = delete;
  RenderWorkflow& operator=(const RenderWorkflow&) = delete;
  ~RenderWorkflow();

  RenderOperation&       addRenderOperation(const RenderOperation& op);
  inline RenderOperation&       getOperation(const std::string& opName);
  inline const RenderOperation& getOperation(const std::string& opName) const;
  //  void buildRenderPasses();
  void compile();
//  void createFrameBuffer();

  std::string                                               name;
  std::unordered_map<std::string, RenderWorkflowAttachment> attachments;
  std::unordered_map<std::string, RenderOperation>          renderOperations;
};


/***************************/

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
class PUMEX_EXPORT RenderPass : public CommandBufferSource
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

  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

LoadOp  loadOpLoad()                        { return LoadOp(LoadOp::Load, glm::vec4(0.0f)); }
LoadOp  loadOpClear(const glm::vec4& color) { return LoadOp(LoadOp::Clear, color); }
LoadOp  loadOpDontCare()                    { return LoadOp(LoadOp::DontCare, glm::vec4(0.0f));}
StoreOp storeOpStore()                      { return StoreOp(StoreOp::Store); }
StoreOp storeOpDontCare()                   { return StoreOp(StoreOp::DontCare); }

	
}