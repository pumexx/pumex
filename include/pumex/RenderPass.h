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
#include <gli/texture.hpp>
#include <pumex/Export.h>
#include <pumex/Command.h>

namespace pumex
{

class  Device;
struct QueueTraits;

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

enum AttachmentType { atUndefined, atSurface, atColor, atDepth, atDepthStencil, atStencil };
inline VkImageAspectFlags getAspectMask(AttachmentType at);

enum AttachmentSizeType { astUndefined, astAbsolute, astSurfaceDependent };

struct AttachmentSize
{
  AttachmentSize()
    : attachmentType{ astUndefined }, imageSize{ 0.0f, 0.0f, 0.0f }
  {
  }
  AttachmentSize(AttachmentSizeType aType, const glm::vec3& imSize)
    : attachmentType{ aType }, imageSize{ imSize }
  {
  }
  AttachmentSize(AttachmentSizeType aType, const glm::vec2& imSize)
    : attachmentType{ aType }, imageSize{ imSize.x, imSize.y, 1.0f }
  {
  }

  AttachmentSizeType attachmentType;
  glm::vec3          imageSize;
};

inline bool operator==(const AttachmentSize& lhs, const AttachmentSize& rhs)
{
  return lhs.attachmentType == rhs.attachmentType && lhs.imageSize == rhs.imageSize;
}

inline bool operator!=(const AttachmentSize& lhs, const AttachmentSize& rhs)
{
  return lhs.attachmentType != rhs.attachmentType || lhs.imageSize != rhs.imageSize;
}

class PUMEX_EXPORT RenderWorkflowResourceType
{
public:
  enum MetaType { Undefined, Attachment, Image, Buffer };
  RenderWorkflowResourceType();
  RenderWorkflowResourceType(const std::string& typeName, VkFormat format, VkSampleCountFlagBits samples, bool persistent, AttachmentType attachmentType, const AttachmentSize& attachmentSize);

  MetaType              metaType;
  std::string           typeName;
  VkFormat              format;
  VkSampleCountFlagBits samples;
  bool                  persistent;

  struct AttachmentData
  {
    AttachmentData(AttachmentType at, const AttachmentSize& as, const gli::swizzles& sw = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA))
      :attachmentType{ at }, attachmentSize{ as }, swizzles{ sw }
    {
    }

    AttachmentType        attachmentType;
    AttachmentSize        attachmentSize;
    gli::swizzles         swizzles;

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

//  VkImageUsageFlags    getUsage() const;

  std::string   name;
  std::string   typeName;
  VkImageLayout operationLayout; // attachments only
  LoadOp        loadOperation;   // attachments only, and maybe images...
};

class NodeGroup;

class PUMEX_EXPORT Node
{
public:
  Node();
  virtual ~Node();
protected:
  std::vector<std::weak_ptr<NodeGroup>> parents;
};

class PUMEX_EXPORT ComputeNode : public Node
{
public:
  ComputeNode();
  virtual ~ComputeNode();

};

class PUMEX_EXPORT NodeGroup : public Node
{
public:
  NodeGroup();
  virtual ~NodeGroup();

  void addChild(std::shared_ptr<Node> child);

protected:
  std::vector<std::shared_ptr<Node>> children;
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

  RenderOperation(const std::string& name, Type operationType, VkSubpassContents subpassContents);
  virtual ~RenderOperation();

  void addAttachmentInput(const WorkflowResource& roAttachment);
  void addAttachmentOutput(const WorkflowResource& attachmentConfig);
  void addAttachmentResolveOutput(const WorkflowResource& attachmentConfig);
  void setAttachmentDepthOutput(const WorkflowResource& attachmentConfig);

  std::vector<const WorkflowResource*> getInputsOutputs(IOType ioTypes) const;

  std::string                                       name;
  Type                                              operationType;
  VkSubpassContents                                 subpassContents;

  std::unordered_map<std::string, WorkflowResource> inputAttachments;
  std::unordered_map<std::string, WorkflowResource> outputAttachments;
  std::unordered_map<std::string, WorkflowResource> resolveAttachments;
  WorkflowResource                                  depthAttachment;

  bool                                              enabled; // not implemented
};

class PUMEX_EXPORT GraphicsOperation : public RenderOperation
{
public:
  GraphicsOperation(const std::string& name, VkSubpassContents subpassContents);

  void setNode(std::shared_ptr<Node> node);

  std::shared_ptr<Node>                             renderNode;
};

class PUMEX_EXPORT ComputeOperation : public RenderOperation
{
public:
  ComputeOperation(const std::string& name, VkSubpassContents subpassContents);

  void setNode(std::shared_ptr<ComputeNode> node);

  std::shared_ptr<ComputeNode>                       computeNode;
};


class PUMEX_EXPORT RenderWorkflow;

class PUMEX_EXPORT RenderWorkflowCompiler
{
public:
  virtual void compile(RenderWorkflow& workflow) = 0;
};

class RenderCommandSequence;



class PUMEX_EXPORT RenderWorkflow
{
public:
  RenderWorkflow()                                 = delete;
  explicit RenderWorkflow(const std::string& name, std::shared_ptr<pumex::RenderWorkflowCompiler> compiler);
  ~RenderWorkflow();

  void                              addResourceType(const RenderWorkflowResourceType& tp);
  const RenderWorkflowResourceType& getResourceType(const std::string& typeName) const;

  void                              addRenderOperation(std::shared_ptr<RenderOperation> op);
  std::shared_ptr<RenderOperation>  getOperation(const std::string& opName) const;

  void                              addQueue(const QueueTraits& queueTraits);

  void getAttachmentSizes(const std::vector<const WorkflowResource*>& resources, std::vector<AttachmentSize>& attachmentSizes) const;
  std::vector<std::shared_ptr<RenderOperation>> findOperations(RenderOperation::IOType ioTypes, const std::vector<const WorkflowResource*>& ioObjects) const;
  std::vector<std::shared_ptr<RenderOperation>> findFinalOperations() const;

  void compile();

  std::string                                                       name;
  std::shared_ptr<pumex::RenderWorkflowCompiler>                    compiler;
  std::unordered_map<std::string, RenderWorkflowResourceType>       resourceTypes;
  std::unordered_map<std::string, std::shared_ptr<RenderOperation>> renderOperations;

  std::vector<QueueTraits>                                          queueTraits;
  std::vector<std::vector<std::shared_ptr<RenderCommandSequence>>>  commandSequences;
  std::vector<std::shared_ptr<pumex::FrameBuffer>>                  frameBuffers;
};

// This is the first implementation of workflow compiler
// It only uses one queue to do the job

struct StandardRenderWorkflowCostCalculator
{
  void  tagOperationByAttachmentType(const RenderWorkflow& workflow);
  float calculateWorkflowCost(const RenderWorkflow& workflow, const std::vector<std::shared_ptr<RenderOperation>>& operationSchedule) const;

  std::unordered_map<std::string, int> attachmentTag;
};

class PUMEX_EXPORT StandardRenderWorkflowCompiler : public RenderWorkflowCompiler
{
public:
  void compile(RenderWorkflow& workflow) override;
private:
  void                                                verifyOperations(RenderWorkflow& workflow);
  std::vector<std::shared_ptr<RenderCommandSequence>> createCommandSequence(const std::vector<std::shared_ptr<RenderOperation>>& operationSequence);
  void                                                collectResources(const std::vector<std::shared_ptr<RenderOperation>>& operationSequence, uint32_t opSeqIndex, std::vector<const WorkflowResource*>& resources, std::unordered_map<std::string, glm::uvec3>& resourceOpRange);
  std::unordered_map<std::string, std::string>        shrinkResources(RenderWorkflow& workflow, const std::vector<const WorkflowResource*>& resources, const std::unordered_map<std::string, glm::uvec3>& resourceOpRange);

  StandardRenderWorkflowCostCalculator                costCalculator;
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

// really - I don't have idea how to name this crucial class :(
class PUMEX_EXPORT RenderCommandSequence : public CommandBufferSource
{
public:
  enum SequenceType{seqRenderPass, seqComputePass};
  RenderCommandSequence(SequenceType sequenceType);
  virtual void setAttachmentDefinitions(const std::vector<AttachmentDefinition>& attachmentDefinitions) = 0;

  SequenceType sequenceType;
};

// class representing Vulkan graphics render pass along with its attachments, subpasses and dependencies
class PUMEX_EXPORT RenderPass : public RenderCommandSequence
{
public:
  RenderPass();
  explicit RenderPass(const std::vector<AttachmentDefinition>& attachments, const std::vector<SubpassDefinition>& subpasses, const std::vector<SubpassDependencyDefinition>& dependencies = std::vector<SubpassDependencyDefinition>());
  RenderPass(const RenderPass&)            = delete;
  RenderPass& operator=(const RenderPass&) = delete;
  ~RenderPass();


  void         validate(Device* device);
  VkRenderPass getHandle(VkDevice device) const;

  void setAttachmentDefinitions(const std::vector<AttachmentDefinition>& attachmentDefinitions) override;

  std::vector<AttachmentDefinition>        attachments;
  std::vector<SubpassDefinition>           subpasses;
  std::vector<SubpassDependencyDefinition> dependencies;

  // render passes
  std::vector<std::shared_ptr<RenderOperation>> renderOperations;
protected:
  struct PerDeviceData
  {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    bool         dirty      = true;
  };

  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

class ComputePass : public RenderCommandSequence
{
public:
  explicit ComputePass();
  void setAttachmentDefinitions(const std::vector<AttachmentDefinition>& attachmentDefinitions) override;

  std::shared_ptr<ComputeOperation> computeOperation;
protected:
  struct PerDeviceData
  {
    bool         dirty = true;
  };

  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

LoadOp             loadOpLoad()                        { return LoadOp(LoadOp::Load, glm::vec4(0.0f)); }
LoadOp             loadOpClear(const glm::vec4& color) { return LoadOp(LoadOp::Clear, color); }
LoadOp             loadOpDontCare()                    { return LoadOp(LoadOp::DontCare, glm::vec4(0.0f));}
StoreOp            storeOpStore()                      { return StoreOp(StoreOp::Store); }
StoreOp            storeOpDontCare()                   { return StoreOp(StoreOp::DontCare); }
VkImageAspectFlags getAspectMask(AttachmentType at)
{
  switch (at)
  {
  case atColor:
  case atSurface:
    return VK_IMAGE_ASPECT_COLOR_BIT;
  case atDepth:
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  case atDepthStencil:
    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  case atStencil:
    return VK_IMAGE_ASPECT_STENCIL_BIT;
  default:
    return (VkImageAspectFlags)0;
  }
  return (VkImageAspectFlags)0;
}

}