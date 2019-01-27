//
// Copyright(c) 2017-2018 Paweł Księżopolski ( pumexx )
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
#include <vector>
#include <set>
#include <list>
#include <map>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#if defined(GLM_ENABLE_EXPERIMENTAL) // hack around redundant GLM_ENABLE_EXPERIMENTAL defined in type.hpp
  #undef GLM_ENABLE_EXPERIMENTAL
  #define GLM_ENABLE_EXPERIMENTAL_HACK
#endif
#include <gli/texture.hpp>
#if defined(GLM_ENABLE_EXPERIMENTAL_HACK)
  #define GLM_ENABLE_EXPERIMENTAL
  #undef GLM_ENABLE_EXPERIMENTAL_HACK
#endif
#include <pumex/Export.h>
#include <pumex/ResourceRange.h>

namespace pumex
{

class Node;

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
inline LoadOp loadOpClear(const glm::vec2& color = glm::vec2(1.0f, 0.0f));
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

enum AttachmentType   { atUndefined, atColor, atDepth, atDepthStencil, atStencil };
enum ResourceMetaType { rmtUndefined, rmtImage, rmtBuffer };
enum OperationType    { opGraphics = VK_QUEUE_GRAPHICS_BIT, opCompute = VK_QUEUE_COMPUTE_BIT, opTransfer = VK_QUEUE_TRANSFER_BIT };
enum OperationEntryType
{
  opeAttachmentInput         = 1,
  opeAttachmentOutput        = 2,
  opeAttachmentResolveOutput = 4,
  opeAttachmentDepthOutput   = 8,
  opeAttachmentDepthInput    = 16,
  opeBufferInput             = 32,
  opeBufferOutput            = 64,
  opeImageInput              = 128,
  opeImageOutput             = 256
};
typedef VkFlags OperationEntryTypeFlags;

const OperationEntryTypeFlags opeAllAttachments       = opeAttachmentInput | opeAttachmentOutput | opeAttachmentResolveOutput | opeAttachmentDepthInput | opeAttachmentDepthOutput;
const OperationEntryTypeFlags opeAllImages            = opeImageInput | opeImageOutput;
const OperationEntryTypeFlags opeAllBuffers           = opeBufferInput | opeBufferOutput;
const OperationEntryTypeFlags opeAllAttachmentInputs  = opeAttachmentInput | opeAttachmentDepthInput;
const OperationEntryTypeFlags opeAllAttachmentOutputs = opeAttachmentOutput | opeAttachmentResolveOutput | opeAttachmentDepthOutput;
const OperationEntryTypeFlags opeAllInputs            = opeAttachmentInput | opeAttachmentDepthInput | opeBufferInput | opeImageInput;
const OperationEntryTypeFlags opeAllOutputs           = opeAttachmentOutput | opeAttachmentResolveOutput | opeAttachmentDepthOutput | opeBufferOutput | opeImageOutput;

const OperationEntryTypeFlags opeAllInputsOutputs     = opeAllInputs | opeAllOutputs;

class PUMEX_EXPORT AttachmentDefinition
{
public:
  AttachmentDefinition();
  AttachmentDefinition(VkFormat format, const ImageSize& attachmentSize, AttachmentType attachmentType, const gli::swizzles& swizzles = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA));

  VkFormat              format;
  ImageSize             attachmentSize;
  AttachmentType        attachmentType;
  gli::swizzles         swizzles;
};

inline bool operator==(const AttachmentDefinition& lhs, const AttachmentDefinition& rhs);
inline bool operator!=(const AttachmentDefinition& lhs, const AttachmentDefinition& rhs);

class PUMEX_EXPORT ResourceDefinition
{
public:
  ResourceDefinition();
  // constructor for images and attachments
  ResourceDefinition(VkFormat format, const ImageSize& attachmentSize, AttachmentType attachmentType, const std::string& name = std::string(), const gli::swizzles& sw = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA));
  // constructor for buffers
  ResourceDefinition(const std::string& name);

  ResourceMetaType      metaType;
  AttachmentDefinition  attachment;
  std::string           name; // external resources must have a name
};

inline bool operator==(const ResourceDefinition& lhs, const ResourceDefinition& rhs);

// swapchain attachment must have name==SWAPCHAIN_NAME
// its definition must be SWAPCHAIN_DEFINITION(VkFormat,arrayLayers) : for example SWAPCHAIN_DEFINITION(VK_FORMAT_B8G8R8A8_UNORM,1)
const std::string               SWAPCHAIN_NAME = "SWAPCHAIN";
PUMEX_EXPORT ResourceDefinition SWAPCHAIN_DEFINITION(VkFormat format, uint32_t arrayLayers = 1 );

class PUMEX_EXPORT RenderOperationEntry
{
public:
  RenderOperationEntry() = default;
  explicit RenderOperationEntry(OperationEntryType entryType, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp, const ImageSubresourceRange& imageRange, VkImageLayout layout, VkImageUsageFlags imageUsage, VkImageCreateFlags imageCreate, VkImageViewType imageViewType, const std::string& resolveSourceEntryName);
  explicit RenderOperationEntry(OperationEntryType entryType, const ResourceDefinition& resourceDefinition, const BufferSubresourceRange& bufferRange, VkPipelineStageFlags pipelineStage, VkAccessFlags accessFlags, VkFormat bufferFormat = VK_FORMAT_UNDEFINED);

  OperationEntryType     entryType;
  ResourceDefinition     resourceDefinition;
  LoadOp                 loadOp;                                               // for images and attachments
  std::string            resolveSourceEntryName;                               // for resolve attachments

  ImageSubresourceRange  imageRange;                                           // used by images
  VkImageLayout          layout          = VK_IMAGE_LAYOUT_UNDEFINED;          // used by attachments and images ( attachments have this value set automaticaly )
  VkImageUsageFlags      imageUsage      = 0;                                  // addidtional imageUsage for image inputs/outputs
  VkImageCreateFlags     imageCreate     = 0;                                  // additional image creation flags
  VkImageViewType        imageViewType   = VK_IMAGE_VIEW_TYPE_MAX_ENUM;        // manual imageViewType. Will be ignored and chosen automatically if == VK_IMAGE_VIEW_TYPE_MAX_ENUM

  BufferSubresourceRange bufferRange;                                          // used by buffers
  VkPipelineStageFlags   pipelineStage   = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; // used by buffers
  VkAccessFlags          accessFlags     = VK_ACCESS_MEMORY_READ_BIT;          // used by buffers
  VkFormat               bufferFormat    = VK_FORMAT_UNDEFINED;                // used by texel buffers probably
};

class PUMEX_EXPORT RenderOperation
{
public:
  RenderOperation();
  RenderOperation(const std::string& name, OperationType operationType = opGraphics, const ImageSize& attachmentSize = ImageSize{ isSurfaceDependent, glm::vec2(1.0f,1.0f), 1, 1, 1 }, uint32_t multiViewMask = 0x0);

  void                  addAttachmentInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp = loadOpLoad(), const ImageSubresourceRange& imageRange = ImageSubresourceRange(), VkImageUsageFlags imageUsage = 0, VkImageCreateFlags imageCreate = 0);
  void                  addAttachmentOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp = loadOpDontCare(), const ImageSubresourceRange& imageRange = ImageSubresourceRange(), VkImageUsageFlags imageUsage = 0, VkImageCreateFlags imageCreate = 0);
  void                  addAttachmentResolveOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp = loadOpDontCare(), const ImageSubresourceRange& imageRange = ImageSubresourceRange(), VkImageUsageFlags imageUsage = 0, VkImageCreateFlags imageCreate = 0, const std::string& sourceEntryName = "");
  void                  setAttachmentDepthInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp = loadOpLoad(), const ImageSubresourceRange& imageRange = ImageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1), VkImageUsageFlags imageUsage = 0, VkImageCreateFlags imageCreate = 0);
  void                  setAttachmentDepthOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp = loadOpDontCare(), const ImageSubresourceRange& imageRange = ImageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1), VkImageUsageFlags imageUsage = 0, VkImageCreateFlags imageCreate = 0);

  void                  addImageInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp = loadOpDontCare(), const ImageSubresourceRange& imageRange = ImageSubresourceRange(), VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED, VkImageUsageFlags imageUsage = 0, VkImageCreateFlags imageCreate = 0, VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM);
  void                  addImageOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const LoadOp& loadOp = loadOpDontCare(), const ImageSubresourceRange& imageRange = ImageSubresourceRange(), VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED, VkImageUsageFlags imageUsage = 0, VkImageCreateFlags imageCreate = 0, VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM);

  void                  addBufferInput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const BufferSubresourceRange& bufferRange = BufferSubresourceRange(), VkPipelineStageFlags pipelineStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VkAccessFlags accessFlags = VK_ACCESS_MEMORY_READ_BIT);
  void                  addBufferOutput(const std::string& entryName, const ResourceDefinition& resourceDefinition, const BufferSubresourceRange& bufferRange = BufferSubresourceRange(), VkPipelineStageFlags pipelineStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VkAccessFlags accessFlags = VK_ACCESS_MEMORY_WRITE_BIT);

  void                  setRenderOperationNode(std::shared_ptr<Node> node);
  std::shared_ptr<Node> getRenderOperationNode();

  std::vector<std::reference_wrapper<const RenderOperationEntry>> getEntries(OperationEntryTypeFlags entryTypes) const;

  std::string           name;
  OperationType         operationType   = opGraphics;
  ImageSize             attachmentSize;
  uint32_t              multiViewMask   = 0;
  bool                  enabled         = true;

  std::map<std::string, RenderOperationEntry> inputEntries;
  std::map<std::string, RenderOperationEntry> outputEntries;
  std::shared_ptr<Node>                       node;
};

inline bool operator<(const RenderOperation& lhs, const RenderOperation& rhs);

class PUMEX_EXPORT ResourceTransition
{
public:
  ResourceTransition() = delete;
  explicit ResourceTransition(uint32_t rteid, uint32_t tid, uint32_t oid, const std::list<RenderOperation>::const_iterator& operation, const std::map<std::string, RenderOperationEntry>::const_iterator& entry, const std::string& externalMemoryObjectName, VkImageLayout externalLayout);

  inline uint32_t                    rteid() const; // identifies specific ResourceTransition connected to specific entry
  inline uint32_t                    tid() const;   // identifies a group of resource transitions ( 1-1, 1-N, N-1 transitions may have the same tid )
  inline uint32_t                    oid() const;   // identifies potential resource ( many transition groups may have the same oid defined )
                                                    // if two transitions have the same tid - they MUST have the same oid
  inline const RenderOperation&      operation() const;
  inline const RenderOperationEntry& entry() const;
  inline const std::string&          operationName() const;
  inline const std::string&          entryName() const;
  inline const std::string&          externalMemoryObjectName() const;
  inline VkImageLayout               externalLayout() const;
  inline const std::list<RenderOperation>::const_iterator                  operationIter() const;
  inline const std::map<std::string, RenderOperationEntry>::const_iterator entryIter() const;
  inline void                        setExternalMemoryObjectName(const std::string& externalMemoryObjectName);

protected:
  uint32_t                                                    rteid_;
  uint32_t                                                    tid_;
  uint32_t                                                    oid_;
  std::list<RenderOperation>::const_iterator                  operation_;
  std::map<std::string, RenderOperationEntry>::const_iterator entry_;
  std::string                                                 externalMemoryObjectName_;
  VkImageLayout                                               externalLayout_;
};

using ResourceTransitionEntry = std::pair<std::string, std::string>;

class PUMEX_EXPORT RenderGraph : public std::enable_shared_from_this<RenderGraph>
{
public:
  RenderGraph()                                 = delete;
  explicit RenderGraph(const std::string& name);
  RenderGraph(const RenderGraph&)            = delete;
  RenderGraph& operator=(const RenderGraph&) = delete;
  RenderGraph(RenderGraph&&)                 = delete;
  RenderGraph& operator=(RenderGraph&&)      = delete;
  ~RenderGraph();

  void                                                          addRenderOperation(const RenderOperation& op);
  // handy function for adding resource transition between two operations ( 1:1 )
  uint32_t                                                      addResourceTransition(const std::string& generatingOperation, const std::string& generatingEntry, const std::string& consumingOperation, const std::string& consumingEntry, uint32_t suggestedObjectID = 0, const std::string& externalMemoryObjectName = std::string());
  // add resource transition between two operations ( 1:1 )
  uint32_t                                                      addResourceTransition(const ResourceTransitionEntry& generator, const ResourceTransitionEntry& consumer, uint32_t suggestedObjectID = 0, const std::string& externalMemoryObjectName = std::string());
  // add transition that has single generating operation and many consuming operations ( 1:N )
  uint32_t                                                      addResourceTransition(const ResourceTransitionEntry& generator, const std::vector<ResourceTransitionEntry>& consumers, uint32_t suggestedObjectID = 0, const std::string& externalMemoryObjectName = std::string());
  // add transition that has many generating operations and single consuming operation ( N:1, all generating transitions must have disjunctive subresource ranges )
  uint32_t                                                      addResourceTransition(const std::vector<ResourceTransitionEntry>& generators, const ResourceTransitionEntry& consumer, uint32_t suggestedObjectID = 0, const std::string& externalMemoryObjectName = std::string());
  // handy function for adding resource transition to outside world ( out:1 )
  uint32_t                                                      addResourceTransitionInput(const std::string& opName, const std::string& entryName, uint32_t suggestedObjectID = 0, const std::string& externalMemoryObjectName = std::string(), VkImageLayout externalLayout = VK_IMAGE_LAYOUT_UNDEFINED);
  // add resource transition between operation and external memory object ( if memory object is not defined, then this is "empty" transition  ( 1:out, or out:1 depending on entry type )
  uint32_t                                                      addResourceTransitionInput(const ResourceTransitionEntry& tran, uint32_t suggestedObjectID = 0, const std::string& externalMemoryObjectName = std::string(), VkImageLayout externalLayout = VK_IMAGE_LAYOUT_UNDEFINED);
  // handy function for adding resource transition to outside world ( 1:out )
  uint32_t                                                      addResourceTransitionOutput(const std::string& opName, const std::string& entryName, uint32_t suggestedObjectID = 0, const std::string& externalMemoryObjectName = std::string(), VkImageLayout externalLayout = VK_IMAGE_LAYOUT_UNDEFINED);
  // add resource transition between operation and external memory object ( if memory object is not defined, then this is "empty" transition  ( 1:out, or out:1 depending on entry type )
  uint32_t                                                      addResourceTransitionOutput(const ResourceTransitionEntry& tran, uint32_t suggestedObjectID = 0, const std::string& externalMemoryObjectName = std::string(), VkImageLayout externalLayout = VK_IMAGE_LAYOUT_UNDEFINED);
  // add missing resource transitions ("empty"). MUST be called before graph compilation
  void                                                          addMissingResourceTransitions();

  std::vector<std::string>                                      getRenderOperationNames() const;
  const RenderOperation&                                        getRenderOperation(const std::string& opName) const;
  RenderOperation&                                              getRenderOperation(const std::string& opName);

  void                                                          setRenderOperationNode(const std::string& opName, std::shared_ptr<Node> node);
  std::shared_ptr<Node>                                         getRenderOperationNode(const std::string& opName);

  std::vector<std::reference_wrapper<const ResourceTransition>> getOperationIO(const std::string& opName, OperationEntryTypeFlags entryTypes) const;
  std::vector<std::reference_wrapper<const ResourceTransition>> getTransitionIO(uint32_t transitionID, OperationEntryTypeFlags entryTypes) const;
  std::vector<std::reference_wrapper<const ResourceTransition>> getObjectIO(uint32_t objectID, OperationEntryTypeFlags entryTypes) const;
  std::reference_wrapper<const ResourceTransition>              getTransition(uint32_t rteid) const;

  inline const std::list<RenderOperation>&                      getOperations() const;
  inline const std::vector<ResourceTransition>&                 getTransitions() const;

  std::string                                                   name;
protected:
  std::list<RenderOperation>                                    operations;
  std::vector<ResourceTransition>                               transitions;

  uint32_t                                                      generateTransitionEntryID();
  uint32_t                                                      generateTransitionID();
  uint32_t                                                      generateObjectID();
  uint32_t                                                      nextTransitionEntryID = 1;
  uint32_t                                                      nextTransitionID = 1;
  uint32_t                                                      nextObjectID = 1;

  bool                                                          valid = false;
};

using RenderOperationSet = std::set<std::reference_wrapper<const RenderOperation>>;

PUMEX_EXPORT RenderOperationSet getInitialOperations(const RenderGraph& renderGraph);
PUMEX_EXPORT RenderOperationSet getFinalOperations(const RenderGraph& renderGraph);
PUMEX_EXPORT RenderOperationSet getPreviousOperations(const RenderGraph& renderGraph, const std::string& opName);
PUMEX_EXPORT RenderOperationSet getNextOperations(const RenderGraph& renderGraph, const std::string& opName);
PUMEX_EXPORT RenderOperationSet getAllPreviousOperations(const RenderGraph& renderGraph, const std::string& opName);
PUMEX_EXPORT RenderOperationSet getAllNextOperations(const RenderGraph& renderGraph, const std::string& opName);

// inlines

LoadOp  loadOpLoad()                        { return LoadOp(LoadOp::Load, glm::vec4(0.0f)); }
LoadOp  loadOpClear(const glm::vec2& color) { return LoadOp(LoadOp::Clear, glm::vec4(color.x, color.y, 0.0f, 0.0f)); }
LoadOp  loadOpClear(const glm::vec4& color) { return LoadOp(LoadOp::Clear, color); }
LoadOp  loadOpDontCare()                    { return LoadOp(LoadOp::DontCare, glm::vec4(0.0f)); }
StoreOp storeOpStore()                      { return StoreOp(StoreOp::Store); }
StoreOp storeOpDontCare()                   { return StoreOp(StoreOp::DontCare); }

bool operator==(const AttachmentDefinition& lhs, const AttachmentDefinition& rhs)
{
  return lhs.format == rhs.format && lhs.attachmentType == rhs.attachmentType && lhs.attachmentSize == rhs.attachmentSize && lhs.swizzles == rhs.swizzles;
}

bool operator!=(const AttachmentDefinition& lhs, const AttachmentDefinition& rhs)
{
  return lhs.format != rhs.format || lhs.attachmentType != rhs.attachmentType || lhs.attachmentSize != rhs.attachmentSize || lhs.swizzles != rhs.swizzles;
}

bool operator==(const ResourceDefinition& lhs, const ResourceDefinition& rhs)
{
  if ( lhs.metaType != rhs.metaType )
    return false;
  if ( ( !lhs.name.empty() || !rhs.name.empty()) && lhs.name == rhs.name )
    return true;
  return lhs.attachment == rhs.attachment;
}

bool operator<(const RenderOperation& lhs, const RenderOperation& rhs) { return lhs.name < rhs.name; }

uint32_t                                                          ResourceTransition::rteid() const                    { return rteid_; }
uint32_t                                                          ResourceTransition::tid() const                      { return tid_; }
uint32_t                                                          ResourceTransition::oid() const                      { return oid_; }
const RenderOperation&                                            ResourceTransition::operation() const                { return *operation_; }
const RenderOperationEntry&                                       ResourceTransition::entry() const                    { return entry_->second; }
const std::string&                                                ResourceTransition::operationName() const            { return operation_->name; }
const std::string&                                                ResourceTransition::entryName() const                { return entry_->first; }
const std::string&                                                ResourceTransition::externalMemoryObjectName() const { return externalMemoryObjectName_; }
VkImageLayout                                                     ResourceTransition::externalLayout() const           { return externalLayout_; }
const std::list<RenderOperation>::const_iterator                  ResourceTransition::operationIter() const            { return operation_; }
const std::map<std::string, RenderOperationEntry>::const_iterator ResourceTransition::entryIter() const                { return entry_; }
void                                                              ResourceTransition::setExternalMemoryObjectName(const std::string& emon) { externalMemoryObjectName_ = emon; }

const std::list<RenderOperation>&                                 RenderGraph::getOperations() const { return operations; }
const std::vector<ResourceTransition>&                            RenderGraph::getTransitions() const { return transitions; }



}
