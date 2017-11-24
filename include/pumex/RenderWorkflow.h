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
struct SubpassDefinition;

struct PUMEX_EXPORT LoadOp
{
  enum Type { Load, Clear, DontCare };
  LoadOp()
    : loadType{ DontCare }, clearColor{}
  {
  }
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
  StoreOp()
    : storeType{ DontCare }
  {
  }
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
inline VkImageUsageFlags  getAttachmentUsage(VkImageLayout imageLayout);

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
//  RenderWorkflowResourceType();
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
  WorkflowResource(const std::string& name, std::shared_ptr<RenderWorkflowResourceType> resourceType);

  std::string                                 name;
  std::shared_ptr<RenderWorkflowResourceType> resourceType;
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

class PUMEX_EXPORT RenderWorkflow;

class PUMEX_EXPORT RenderOperation
{
public:
  enum Type { Graphics, Compute };

  RenderOperation(const std::string& name, Type operationType, VkSubpassContents subpassContents);
  virtual ~RenderOperation();

  void setRenderWorkflow ( std::shared_ptr<RenderWorkflow> renderWorkflow );

  SubpassDefinition buildSubPassDefinition(const std::unordered_map<std::string, uint32_t>& resourceIndex) const;


  std::string                                       name;
  Type                                              operationType;
  VkSubpassContents                                 subpassContents;
  std::weak_ptr<RenderWorkflow>                     renderWorkflow;

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

enum ResourceTransitionType 
{
  rttAttachmentInput         = 1, 
  rttAttachmentOutput        = 2,
  rttAttachmentResolveOutput = 4,
  rttAttachmentDepthOutput   = 8,
  rttAllAttachments          = (rttAttachmentInput | rttAttachmentOutput | rttAttachmentResolveOutput | rttAttachmentDepthOutput),
  rttAllInputs               = (rttAttachmentInput),
  rttAllOutputs              = (rttAttachmentOutput | rttAttachmentResolveOutput | rttAttachmentDepthOutput),
  rttAllInputsOutputs        = (rttAllInputs | rttAllOutputs)
};


class PUMEX_EXPORT ResourceTransition
{
public:
  ResourceTransition(std::shared_ptr<RenderOperation> operation, std::shared_ptr<WorkflowResource> resource, ResourceTransitionType transitionType, VkImageLayout layout, const LoadOp& load);
  std::shared_ptr<RenderOperation>  operation;
  std::shared_ptr<WorkflowResource> resource;
  ResourceTransitionType            transitionType;
  std::shared_ptr<WorkflowResource> resolveResource;
  VkImageLayout                     layout;
  LoadOp                            load;
};


class PUMEX_EXPORT RenderWorkflowCompiler
{
public:
  virtual void compile(RenderWorkflow& workflow) = 0;
};

// really - I don't have idea how to name this crucial class :(
class PUMEX_EXPORT RenderCommand : public CommandBufferSource
{
public:
  enum CommandType{commRenderPass, commComputePass};
  RenderCommand(CommandType commandType);
//  virtual void setAttachmentDefinitions(const std::vector<AttachmentDefinition>& attachmentDefinitions) = 0;

  CommandType commandType;
};

class PUMEX_EXPORT RenderWorkflow : public std::enable_shared_from_this<RenderWorkflow>
{
public:
  RenderWorkflow()                                 = delete;
  explicit RenderWorkflow(const std::string& name, std::shared_ptr<pumex::RenderWorkflowCompiler> compiler);
  ~RenderWorkflow();

  void                                        addResourceType(std::shared_ptr<RenderWorkflowResourceType> tp);
  std::shared_ptr<RenderWorkflowResourceType> getResourceType(const std::string& typeName) const;

  void                                        addRenderOperation(std::shared_ptr<RenderOperation> op);
  std::shared_ptr<RenderOperation>            getRenderOperation(const std::string& opName) const;

  std::shared_ptr<WorkflowResource>           getResource(const std::string& resourceName) const;

  void addAttachmentInput(const std::string& opName, const std::string& resourceName, const std::string& resourceType, VkImageLayout layout);
  void addAttachmentOutput(const std::string& opName, const std::string& resourceName, const std::string& resourceType, VkImageLayout layout, const LoadOp& loadOp);
  void addAttachmentResolveOutput(const std::string& opName, const std::string& resourceName, const std::string& resourceType, const std::string& resourceSource, VkImageLayout layout, const LoadOp& loadOp);
  void addAttachmentDepthOutput(const std::string& opName, const std::string& resourceName, const std::string& resourceType, VkImageLayout layout, const LoadOp& loadOp);

  std::vector<std::shared_ptr<ResourceTransition>> getOperationIO(const std::string& opName, ResourceTransitionType transitionTypes) const;
  std::vector<std::shared_ptr<ResourceTransition>> getResourceIO(const std::string& resourceName, ResourceTransitionType transitionTypes) const;

  std::vector<std::shared_ptr<RenderOperation>> getPreviousOperations(const std::string& opName);
  std::vector<std::shared_ptr<RenderOperation>> getNextOperations(const std::string& opName);


  void                                             addQueue(const QueueTraits& queueTraits);

//  void getAttachmentSizes(const std::vector<const WorkflowResource*>& resources, std::vector<AttachmentSize>& attachmentSizes) const;
//  std::vector<std::shared_ptr<RenderOperation>> findOperations(RenderOperation::IOType ioTypes, const std::vector<const WorkflowResource*>& ioObjects) const;
//  std::vector<std::shared_ptr<RenderOperation>> findFinalOperations() const;

  void compile();

  std::string                                                                  name;
  std::shared_ptr<pumex::RenderWorkflowCompiler>                               compiler;
  std::unordered_map<std::string, std::shared_ptr<RenderWorkflowResourceType>> resourceTypes;
  std::unordered_map<std::string, std::shared_ptr<RenderOperation>>            renderOperations;
  std::unordered_map<std::string, std::shared_ptr<WorkflowResource>>           resources;
  std::vector<std::shared_ptr<ResourceTransition>>                             transitions;

  std::vector<QueueTraits>                                                     queueTraits;
  std::vector<std::vector<std::shared_ptr<RenderCommand>>>                     commandSequences;
  std::vector<std::shared_ptr<pumex::FrameBuffer>>                             frameBuffers;
};

// This is the first implementation of workflow compiler
// It only uses one queue to do the job

struct PUMEX_EXPORT StandardRenderWorkflowCostCalculator
{
  void  tagOperationByAttachmentType(const RenderWorkflow& workflow);
  float calculateWorkflowCost(const RenderWorkflow& workflow, const std::vector<std::shared_ptr<RenderOperation>>& operationSchedule) const;

  std::unordered_map<std::string, int> attachmentTag;
};

class PUMEX_EXPORT SingleQueueWorkflowCompiler : public RenderWorkflowCompiler
{
public:
  void compile(RenderWorkflow& workflow) override;
private:
  void                                        verifyOperations(RenderWorkflow& workflow);
  void                                        collectResources(RenderWorkflow& workflow, std::vector<std::shared_ptr<WorkflowResource>>& resourceVector, std::unordered_map<std::string, uint32_t>& resourceIndex);
  std::vector<std::shared_ptr<RenderCommand>> createCommandSequence(const std::vector<std::shared_ptr<RenderOperation>>& operationSequence);

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

VkImageUsageFlags  getAttachmentUsage(VkImageLayout il)
{
  switch (il)
  {
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:         return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:         return VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:             return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:             return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
  case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:               return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  case VK_IMAGE_LAYOUT_UNDEFINED:
  case VK_IMAGE_LAYOUT_GENERAL:
  case VK_IMAGE_LAYOUT_PREINITIALIZED:
  default:                                               return (VkImageUsageFlags)0;
  }
  return (VkImageUsageFlags)0;
}



	
}