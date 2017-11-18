//
// Copyright(c) 2017 Paweł Księżopolski ( pumexx )
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
    : attachmentSize{ astUndefined }, imageSize{ 0.0f, 0.0f, 0.0f }
  {
  }
  AttachmentSize(AttachmentSizeType aSize, const glm::vec3& imSize)
    : attachmentSize{ aSize }, imageSize{ imSize }
  {
  }
  AttachmentSize(AttachmentSizeType aSize, const glm::vec2& imSize)
    : attachmentSize{ aSize }, imageSize{ imSize.x, imSize.y, 1.0f }
  {
  }

  AttachmentSizeType attachmentSize;
  glm::vec3          imageSize;
};

inline bool operator==(const AttachmentSize& lhs, const AttachmentSize& rhs)
{
  return lhs.attachmentSize == rhs.attachmentSize && lhs.imageSize == rhs.imageSize;
}

inline bool operator!=(const AttachmentSize& lhs, const AttachmentSize& rhs)
{
  return lhs.attachmentSize != rhs.attachmentSize || lhs.imageSize != rhs.imageSize;
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

// really - I don't have idea how to name this crucial class :(
class PUMEX_EXPORT RenderCommandSequence : public CommandBufferSource
{
public:
  enum SequenceType{seqRenderPass, seqComputePass};
  RenderCommandSequence(SequenceType sequenceType);
//  virtual void setAttachmentDefinitions(const std::vector<AttachmentDefinition>& attachmentDefinitions) = 0;

  SequenceType sequenceType;
};

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