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

enum AttachmentType { atSurface, atColor, atDepth };
enum AttachmentSize { asUndefined, asAbsolute, asSurfaceDependent };

class PUMEX_EXPORT RenderWorkflowResourceType
{
public:
  enum MetaType { Undefined, Attachment, Image, Buffer };
  RenderWorkflowResourceType();
  RenderWorkflowResourceType(const std::string& typeName, VkFormat format, VkSampleCountFlagBits samples, bool persistent, AttachmentType attachmentType, AttachmentSize sizeType, glm::vec2 imageSize);

  MetaType              metaType;
  std::string           typeName;
  VkFormat              format;
  VkSampleCountFlagBits samples;
  bool                  persistent;

  struct AttachmentData
  {
    AttachmentData(AttachmentType at, AttachmentSize st, glm::vec2 is)
      :attachmentType{ at }, sizeType{ st }, imageSize{ is }
    {
    }
    AttachmentType        attachmentType;
    AttachmentSize        sizeType;
    glm::vec2             imageSize;
  };

  union
  {
    AttachmentData attachment;
  };
};

class PUMEX_EXPORT WorkflowResource
{
public:
  WorkflowResource();
  WorkflowResource(const std::string& name, const std::string& typeName, VkImageLayout operationLayout);
  WorkflowResource(const std::string& name, const std::string& typeName, VkImageLayout operationLayout, LoadOp loadOperation);

  std::string   name;
  std::string   typeName;
  VkImageLayout operationLayout;
  LoadOp        loadOperation;
};

class PUMEX_EXPORT RenderOperation
{
public:
  enum Type { Graphics, Compute };
  enum IOType { AttachmentInput = 1, AttachmentOutput = 2, AttachmentResolveOutput = 4, AttachmentDepthOutput = 8, 
    AllAttachments = (AttachmentInput | AttachmentOutput | AttachmentResolveOutput | AttachmentDepthOutput), 
    AllInputs  = (AttachmentInput),
    AllOutputs = (AttachmentOutput | AttachmentResolveOutput | AttachmentDepthOutput),
    AllInputsOutputs = ( AllInputs | AllOutputs ) };
  RenderOperation();
  RenderOperation(const std::string& name, Type operationType);

  void addAttachmentInput(const WorkflowResource& roAttachment);
  void addAttachmentOutput(const WorkflowResource& attachmentConfig);
  void addAttachmentResolveOutput(const WorkflowResource& attachmentConfig);
  void setAttachmentDepthOutput(const WorkflowResource& attachmentConfig);

  std::vector<const WorkflowResource*> getInputsOutputs(IOType ioTypes) const;

  std::string                                       name;
  Type                                              operationType;
  std::unordered_map<std::string, WorkflowResource> inputAttachments;
  std::unordered_map<std::string, WorkflowResource> outputAttachments;
  std::unordered_map<std::string, WorkflowResource> resolveAttachments;
  WorkflowResource                                  depthAttachment;
  bool                                              enabled;
};

class PUMEX_EXPORT RenderWorkflow;

class PUMEX_EXPORT RenderWorkflowCompiler
{
public:
  virtual void compile(RenderWorkflow& workflow) = 0;
};

class PUMEX_EXPORT RenderWorkflow
{
public:
  RenderWorkflow()                                 = delete;
  explicit RenderWorkflow(const std::string& name);
//  RenderWorkflow(const RenderWorkflow&)            = delete;
//  RenderWorkflow& operator=(const RenderWorkflow&) = delete;
  ~RenderWorkflow();

  void                              addResourceType(const RenderWorkflowResourceType& tp);
  const RenderWorkflowResourceType& getResourceType(const std::string& typeName) const;

  RenderOperation&                  addRenderOperation(const RenderOperation& op);
  const RenderOperation&            getOperation(const std::string& opName) const;

  void getAttachmentSizes(const std::vector<const WorkflowResource*>& resources, std::vector<AttachmentSize>& attachmentSizes, std::vector<glm::vec2>& imageSizes) const;
  std::vector<const RenderOperation*> findOperations(RenderOperation::IOType ioTypes, const std::vector<const WorkflowResource*>& ioObjects) const;
  std::vector<std::string> findFinalOperations() const;

  void compile(RenderWorkflowCompiler* compiler);

  std::string                                                 name;
  std::unordered_map<std::string, RenderWorkflowResourceType> resourceTypes;
  std::unordered_map<std::string, RenderOperation>            renderOperations;
};

class PUMEX_EXPORT StandardRenderWorkflowCompiler : public RenderWorkflowCompiler
{
public:
  void compile(RenderWorkflow& workflow) override;
private:
  void                                    verifyOperations(RenderWorkflow& workflow);
  std::vector<std::string>                scheduleOperations(RenderWorkflow& workflow);

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