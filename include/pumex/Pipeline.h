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
#include <set>
#include <unordered_map>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/Asset.h>
#include <pumex/Command.h>

namespace pumex
{

class Device;
class Surface;
class RenderPass;

// A set of classes implementing different Vulkan pipeline elements

// descriptor set layout creation
struct PUMEX_EXPORT DescriptorSetLayoutBinding
{
  DescriptorSetLayoutBinding(uint32_t binding, uint32_t bindingCount, VkDescriptorType descriptorType, VkShaderStageFlags stageFlags);
  uint32_t            binding        = 0;
  uint32_t            bindingCount   = 1;
  VkDescriptorType    descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT
  VkShaderStageFlags  stageFlags     = VK_SHADER_STAGE_ALL_GRAPHICS; // VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_GEOMETRY_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_ALL_GRAPHICS
};


class PUMEX_EXPORT DescriptorSetLayout
{
public:
  DescriptorSetLayout()                                      = delete;
  explicit DescriptorSetLayout(const std::vector<DescriptorSetLayoutBinding>& bindings);
  DescriptorSetLayout(const DescriptorSetLayout&)            = delete;
  DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;
  virtual ~DescriptorSetLayout();

  void                  validate(Device* device);
  VkDescriptorSetLayout getHandle(VkDevice device) const;
  VkDescriptorType      getDescriptorType(uint32_t binding) const;
  uint32_t              getDescriptorBindingCount(uint32_t binding) const;

  std::vector<DescriptorSetLayoutBinding> bindings;
protected:
  struct PerDeviceData
  {
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  };
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

class PUMEX_EXPORT DescriptorPool
{
public:
  explicit DescriptorPool(uint32_t poolSize, const std::vector<DescriptorSetLayoutBinding>& bindings);
  DescriptorPool(const DescriptorPool&)            = delete;
  DescriptorPool& operator=(const DescriptorPool&) = delete;
  virtual ~DescriptorPool();

  void             validate(Device* device);
  VkDescriptorPool getHandle(VkDevice device) const;

  uint32_t poolSize;
  std::vector<DescriptorSetLayoutBinding> bindings;
protected:
  struct PerDeviceData
  {
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  };
  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

struct PUMEX_EXPORT DescriptorSetValue
{
  enum Type { Undefined, Image, Buffer };
  DescriptorSetValue();
  DescriptorSetValue(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
  DescriptorSetValue(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);

  Type vType;
  union
  {
    VkDescriptorBufferInfo bufferInfo;
    VkDescriptorImageInfo  imageInfo;
  };
};

class DescriptorSet;

class PUMEX_EXPORT DescriptorSetSource
{
public:
  virtual ~DescriptorSetSource();
  virtual void addDescriptorSet(DescriptorSet* descriptorSet);
  virtual void removeDescriptorSet(DescriptorSet* descriptorSet);
  virtual void getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const
  {
  }
  virtual void getDescriptorSetValues(VkSurfaceKHR surface, uint32_t index, std::vector<DescriptorSetValue>& values) const
  {
  }
  virtual void notifyDescriptorSets();
protected:
  std::set<DescriptorSet*> descriptorSets;
};

// class not used ATM, but I will leave it for now
//class PUMEX_EXPORT DescriptorSetSourceArray : public DescriptorSetSource
//{
//public:
//  DescriptorSetSourceArray() = default;
//  void addSource(std::shared_ptr<DescriptorSetSource> source);
//  void addDescriptorSet(DescriptorSet* descriptorSet) override;
//  void removeDescriptorSet(DescriptorSet* descriptorSet) override;
//
//  void getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const override;
//  void getDescriptorSetValues(VkSurfaceKHR surface, uint32_t index, std::vector<DescriptorSetValue>& values) const override;
//
//  void notifyDescriptorSets() override;
//protected:
//  std::vector<std::shared_ptr<DescriptorSetSource>> sources;
//};

class PUMEX_EXPORT DescriptorSet : public CommandBufferSource
{
public:
  explicit DescriptorSet(std::shared_ptr<DescriptorSetLayout> layout, std::shared_ptr<DescriptorPool> pool, uint32_t activeCount = 1);
  DescriptorSet(const DescriptorSet&) = delete;
  DescriptorSet& operator=(const DescriptorSet&) = delete;
  virtual ~DescriptorSet();

  inline void setActiveIndex(uint32_t index);
  inline uint32_t getActiveIndex() const;

  void            validate(Surface* surface);
  VkDescriptorSet getHandle(VkSurfaceKHR surface) const;
  void            setDirty();
  void            setSource(uint32_t binding, std::shared_ptr<DescriptorSetSource> source);
  void            resetSource(uint32_t binding);

  std::shared_ptr<DescriptorSetLayout>                              layout;
  std::shared_ptr<DescriptorPool>                                   pool;
protected:
  struct PerSurfaceData
  {
    PerSurfaceData(uint32_t ac, VkDevice d)
      : device{ d }
    {
      descriptorSet.resize(ac,VK_NULL_HANDLE);
      dirty.resize(ac,true);
    }
    std::vector<VkDescriptorSet> descriptorSet;
    std::vector<bool>            dirty;
    VkDevice                     device;
  };

  mutable std::mutex                                                 mutex;
  std::unordered_map<VkSurfaceKHR, PerSurfaceData>                   perSurfaceData;
  std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetSource>> sources; // descriptor set owns the buffers, images and whatnot
  uint32_t                                                           activeCount;
  uint32_t                                                           activeIndex = 0;
};

void DescriptorSet::setActiveIndex(uint32_t index) { activeIndex = index % activeCount; }
uint32_t DescriptorSet::getActiveIndex() const     { return activeIndex; }

class PUMEX_EXPORT PipelineLayout
{
public:
  explicit PipelineLayout();
  PipelineLayout(const PipelineLayout&)            = delete;
  PipelineLayout& operator=(const PipelineLayout&) = delete;
  virtual ~PipelineLayout();
  void             validate(Device* device);
  VkPipelineLayout getHandle(VkDevice device) const;

  std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayouts;
protected:
  struct PerDeviceData
  {
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  };

  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};


class PUMEX_EXPORT PipelineCache
{
public:
  explicit PipelineCache();
  PipelineCache(const PipelineCache&)            = delete;
  PipelineCache& operator=(const PipelineCache&) = delete;
  virtual ~PipelineCache();

  void            validate(Device* device);
  VkPipelineCache getHandle(VkDevice device) const;

protected:
  struct PerDeviceData
  {
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
  };
  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};


// pipeline creation
struct PUMEX_EXPORT VertexInputDefinition
{
  VertexInputDefinition(uint32_t binding, VkVertexInputRate inputRate, const std::vector<VertexSemantic>& semantic);
  uint32_t                    binding   = 0;
  VkVertexInputRate           inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  std::vector<VertexSemantic> semantic;
};

struct PUMEX_EXPORT BlendAttachmentDefinition
{
  BlendAttachmentDefinition(VkBool32 blendEnable, VkColorComponentFlags colorWriteMask, VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE, VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO, VkBlendOp colorBlendOp = VK_BLEND_OP_ADD, VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD);
  VkBool32                 blendEnable         = VK_FALSE;
  VkColorComponentFlags    colorWriteMask      = 0xF;
  VkBlendFactor            srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  VkBlendFactor            dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  VkBlendOp                colorBlendOp        = VK_BLEND_OP_ADD;
  VkBlendFactor            srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  VkBlendFactor            dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  VkBlendOp                alphaBlendOp        = VK_BLEND_OP_ADD;
};

class PUMEX_EXPORT ShaderModule
{
public:
  ShaderModule()                               = delete;
  explicit ShaderModule( const std::string& fileName );
  ShaderModule(const ShaderModule&)            = delete;
  ShaderModule& operator=(const ShaderModule&) = delete;
  virtual ~ShaderModule();

  void           validate(Device* device);
  VkShaderModule getHandle(VkDevice device) const;

  std::string fileName;
  std::string shaderContents;
protected:
  struct PerDeviceData
  {
    VkShaderModule  shaderModule = VK_NULL_HANDLE;
  };
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};


struct PUMEX_EXPORT ShaderStageDefinition
{
  ShaderStageDefinition();
  ShaderStageDefinition(VkShaderStageFlagBits stage, std::shared_ptr<ShaderModule> shaderModule, const std::string& entryPoint = "main");
  VkShaderStageFlagBits         stage;
  std::shared_ptr<ShaderModule> shaderModule;
  std::string                   entryPoint = "main";
};

class PUMEX_EXPORT GraphicsPipeline : public CommandBufferSource
{
public:
  GraphicsPipeline()                                   = delete;
  explicit GraphicsPipeline(std::shared_ptr<PipelineCache> pipelineCache, std::shared_ptr<PipelineLayout> pipelineLayout, std::shared_ptr<RenderPass> renderPass, uint32_t subpass);
  GraphicsPipeline(const GraphicsPipeline&)            = delete;
  GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
  virtual ~GraphicsPipeline();

  inline bool hasDynamicState(VkDynamicState state) const;
  inline bool hasShaderStage(VkShaderStageFlagBits stage) const;
  // FIXME : add a bunch of handy functions defining different pipeline aspects

  void       validate(Device* device);
  VkPipeline getHandle(VkDevice device) const;
  void       setDirty();

  // vertex input state
  std::vector<VertexInputDefinition> vertexInput;

  // assembly state
  VkPrimitiveTopology                        topology                = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkBool32                                   primitiveRestartEnable  = VK_FALSE;

  // tessellation state
  uint32_t                                   patchControlPoints      = 0;

  // rasterization state
  VkBool32                                   depthClampEnable        = VK_FALSE;
  VkBool32                                   rasterizerDiscardEnable = VK_FALSE;
  VkPolygonMode                              polygonMode             = VK_POLYGON_MODE_FILL;
  VkCullModeFlags                            cullMode                = VK_CULL_MODE_BACK_BIT;
  VkFrontFace                                frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  VkBool32                                   depthBiasEnable         = VK_FALSE;
  float                                      depthBiasConstantFactor = 0.0f;
  float                                      depthBiasClamp          = 0.0f;
  float                                      depthBiasSlopeFactor    = 0.0f;
  float                                      lineWidth               = 1.0f;

  // blend state
  std::vector<BlendAttachmentDefinition>     blendAttachments;

  // depth and stencil state
  VkBool32                                  depthTestEnable          = VK_TRUE;
  VkBool32                                  depthWriteEnable         = VK_TRUE;
  VkCompareOp                               depthCompareOp           = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkBool32                                  depthBoundsTestEnable    = VK_FALSE;
  VkBool32                                  stencilTestEnable        = VK_FALSE;
  VkStencilOpState                          front;                   // defined in constructor
  VkStencilOpState                          back;                    // defined in constructor
  float                                     minDepthBounds           = 0.0f;
  float                                     maxDepthBounds           = 0.0f;

  // viewport and scissor
  std::vector<VkViewport>                   viewports;
  std::vector<VkRect2D>                     scissors;
  std::vector<VkDynamicState>               dynamicStates; // VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH, VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_DEPTH_BOUNDS, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE

  // multisample support
  VkSampleCountFlagBits                    rasterizationSamples      = VK_SAMPLE_COUNT_1_BIT;
  VkBool32                                 sampleShadingEnable       = VK_FALSE;
  float                                    minSampleShading          = 0.0f;
  const VkSampleMask*                      pSampleMask               = nullptr;
  VkBool32                                 alphaToCoverageEnable     = VK_FALSE;
  VkBool32                                 alphaToOneEnable          = VK_FALSE;

  // shaderstages
  std::vector<ShaderStageDefinition>       shaderStages;
protected:
  std::shared_ptr<PipelineCache>           pipelineCache;
  std::shared_ptr<PipelineLayout>          pipelineLayout;
  std::shared_ptr<RenderPass>              renderPass;
  uint32_t                                 subpass;

  struct PerDeviceData
  {
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool       dirty    = true;
  };

  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

class PUMEX_EXPORT ComputePipeline : public CommandBufferSource
{
public:
  ComputePipeline()                                  = delete;
  explicit ComputePipeline(std::shared_ptr<PipelineCache> pipelineCache, std::shared_ptr<PipelineLayout> pipelineLayout);
  ComputePipeline(const ComputePipeline&)            = delete;
  ComputePipeline& operator=(const ComputePipeline&) = delete;
  virtual ~ComputePipeline();

  void       validate(Device* device);
  VkPipeline getHandle(VkDevice device) const;
  void       setDirty();

  // shader stage
  ShaderStageDefinition             shaderStage;
protected:
  std::shared_ptr<PipelineCache>    pipelineCache;
  std::shared_ptr<PipelineLayout>   pipelineLayout;

  struct PerDeviceData
  {
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool       dirty = true;
  };

  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};


bool GraphicsPipeline::hasDynamicState(VkDynamicState state) const
{
  for (const auto& d : dynamicStates)
  {
    if (d==state)
      return true;
  }
  return false;
}

bool GraphicsPipeline::hasShaderStage(VkShaderStageFlagBits stage) const
{
  for (const auto& s : shaderStages)
  {
    if (s.stage == stage)
      return true;
  }
  return false;
}


}