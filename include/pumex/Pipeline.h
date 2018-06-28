//
// Copyright(c) 2017-2018 Pawe³ Ksiê¿opolski ( pumexx )
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
#include <pumex/Node.h>

namespace pumex
{

class DescriptorSetLayout;

class PUMEX_EXPORT PipelineLayout
{
public:
  explicit PipelineLayout();
  PipelineLayout(const PipelineLayout&)            = delete;
  PipelineLayout& operator=(const PipelineLayout&) = delete;
  PipelineLayout(PipelineLayout&&)                 = delete;
  PipelineLayout& operator=(PipelineLayout&&)      = delete;
  virtual ~PipelineLayout();

  void             validate(const RenderContext& renderContext);
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
  PipelineCache(PipelineCache&&)                 = delete;
  PipelineCache& operator=(PipelineCache&&)      = delete;
  virtual ~PipelineCache();

  void            validate(const RenderContext& renderContext);
  VkPipelineCache getHandle(VkDevice device) const;

protected:
  struct PerDeviceData
  {
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
  };
  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

class PUMEX_EXPORT Pipeline : public Group
{
public:
  Pipeline()                           = delete;
  explicit Pipeline(std::shared_ptr<PipelineCache> pipelineCache, std::shared_ptr<PipelineLayout> pipelineLayout);
  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;
  Pipeline(Pipeline&&)                 = delete;
  Pipeline& operator=(Pipeline&&)      = delete;
  virtual ~Pipeline();

  VkPipeline getHandlePipeline(const RenderContext& renderContext) const;

  // TODO : add descriptor set checking, add dynamic state checking

  std::shared_ptr<PipelineCache>    pipelineCache;
  std::shared_ptr<PipelineLayout>   pipelineLayout;
protected:
  struct PipelineInternal
  {
    PipelineInternal()
      : pipeline{ VK_NULL_HANDLE }
    {
    }
    VkPipeline pipeline;
  };
  typedef PerObjectData<PipelineInternal, uint32_t> PipelineData;

  std::unordered_map<uint32_t, PipelineData> perDeviceData;
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
  explicit ShaderModule( const filesystem::path& fileName );
  ShaderModule(const ShaderModule&)            = delete;
  ShaderModule& operator=(const ShaderModule&) = delete;
  ShaderModule(ShaderModule&&)                 = delete;
  ShaderModule& operator=(ShaderModule&&)      = delete;
  virtual ~ShaderModule();

  void           validate(const RenderContext& renderContext);
  VkShaderModule getHandle(VkDevice device) const;

  filesystem::path fileName;
  std::string      shaderContents;
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

class PUMEX_EXPORT GraphicsPipeline : public Pipeline
{
public:
  GraphicsPipeline()                                   = delete;
  explicit GraphicsPipeline(std::shared_ptr<PipelineCache> pipelineCache, std::shared_ptr<PipelineLayout> pipelineLayout);
  GraphicsPipeline(const GraphicsPipeline&)            = delete;
  GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
  GraphicsPipeline(GraphicsPipeline&&)                 = delete;
  GraphicsPipeline& operator=(GraphicsPipeline&&)      = delete;
  virtual ~GraphicsPipeline();

  inline bool hasDynamicState(VkDynamicState state) const;
  inline bool hasShaderStage(VkShaderStageFlagBits stage) const;

  // TODO : add a bunch of handy functions defining different pipeline aspects - these functions must call internalInvalidate()
  void       accept(NodeVisitor& visitor) override;
  void       validate(const RenderContext& renderContext) override;

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
  VkSampleCountFlagBits                     rasterizationSamples      = VK_SAMPLE_COUNT_1_BIT;
  VkBool32                                  sampleShadingEnable       = VK_FALSE;
  float                                     minSampleShading          = 0.0f;
  const VkSampleMask*                       pSampleMask               = nullptr;
  VkBool32                                  alphaToCoverageEnable     = VK_FALSE;
  VkBool32                                  alphaToOneEnable          = VK_FALSE;

  // shaderstages
  std::vector<ShaderStageDefinition>        shaderStages;
};

class PUMEX_EXPORT ComputePipeline : public Pipeline
{
public:
  ComputePipeline()                                  = delete;
  explicit ComputePipeline(std::shared_ptr<PipelineCache> pipelineCache, std::shared_ptr<PipelineLayout> pipelineLayout);
  ComputePipeline(const ComputePipeline&)            = delete;
  ComputePipeline& operator=(const ComputePipeline&) = delete;
  ComputePipeline(ComputePipeline&&)                 = delete;
  ComputePipeline& operator=(ComputePipeline&&)      = delete;
  virtual ~ComputePipeline();

  // TODO : add a bunch of handy functions defining different pipeline aspects - these functions must call internalInvalidate()
  void       accept(NodeVisitor& visitor) override;
  void       validate(const RenderContext& renderContext) override;

  // shader stage
  ShaderStageDefinition             shaderStage;
};


bool GraphicsPipeline::hasDynamicState(VkDynamicState state) const
{
  for (const auto& d : dynamicStates)
    if (d==state)
      return true;
  return false;
}

bool GraphicsPipeline::hasShaderStage(VkShaderStageFlagBits stage) const
{
  for (const auto& s : shaderStages)
    if (s.stage == stage)
      return true;
  return false;
}


}