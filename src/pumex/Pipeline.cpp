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

#include <pumex/Pipeline.h>
#include <pumex/Descriptor.h>
#include <pumex/NodeVisitor.h>
#include <pumex/RenderPass.h>
#include <pumex/utils/Log.h>
#include <pumex/RenderContext.h>
#include <fstream>

using namespace pumex;

PipelineLayout::PipelineLayout()
{
}

PipelineLayout::~PipelineLayout()
{
  for (auto& pddit : perDeviceData)
    vkDestroyPipelineLayout(pddit.first, pddit.second.pipelineLayout, nullptr);
}

void PipelineLayout::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit != end(perDeviceData))
    return;
  pddit = perDeviceData.insert( { renderContext.vkDevice, PerDeviceData()}).first;

  VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    std::vector<VkDescriptorSetLayout> descriptors;
    for (auto& dsl : descriptorSetLayouts)
    { 
      dsl->validate(renderContext);
      descriptors.push_back(dsl->getHandle(renderContext));
    }
    pipelineLayoutCI.setLayoutCount = descriptors.size();
    pipelineLayoutCI.pSetLayouts    = descriptors.data();
  VK_CHECK_LOG_THROW(vkCreatePipelineLayout(pddit->first, &pipelineLayoutCI, nullptr, &pddit->second.pipelineLayout), "Cannot create pipeline layout");
}

VkPipelineLayout PipelineLayout::getHandle(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == end(perDeviceData))
    return VK_NULL_HANDLE;
  return pddit->second.pipelineLayout;
}

PipelineCache::PipelineCache()
{
}

PipelineCache::~PipelineCache()
{
  for (auto& pddit : perDeviceData)
    vkDestroyPipelineCache(pddit.first, pddit.second.pipelineCache, nullptr);
}

void PipelineCache::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit != end(perDeviceData))
    return;
  pddit = perDeviceData.insert({ renderContext.vkDevice,PerDeviceData()}).first;

  VkPipelineCacheCreateInfo pipelineCacheCI{};
    pipelineCacheCI.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  VK_CHECK_LOG_THROW(vkCreatePipelineCache(pddit->first, &pipelineCacheCI, nullptr, &pddit->second.pipelineCache), "Cannot create pipeline cache");
}

VkPipelineCache PipelineCache::getHandle(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == end(perDeviceData))
    return VK_NULL_HANDLE;
  return pddit->second.pipelineCache;
}

ShaderModule::ShaderModule(const std::string& f)
  : fileName(f)
{
  std::ifstream file(fileName.c_str(),std::ios::in | std::ios::binary);
  CHECK_LOG_THROW(!file, "Cannot read shader file : " << fileName);
  file.seekg(0, std::ios::end);
  std::ifstream::pos_type pos = file.tellg();
  file.seekg(0, std::ios::beg);
  shaderContents.resize(pos);
  file.read(&shaderContents[0], pos);
}

ShaderModule::~ShaderModule()
{
  for (auto& pddit : perDeviceData)
    vkDestroyShaderModule(pddit.first, pddit.second.shaderModule, nullptr);
}

void ShaderModule::validate(const RenderContext& renderContext)
{
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit != end(perDeviceData))
    return;
  pddit = perDeviceData.insert({ renderContext.vkDevice, PerDeviceData() }).first;

  VkShaderModuleCreateInfo shaderModuleCI{};
    shaderModuleCI.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCI.codeSize = shaderContents.size();
    shaderModuleCI.pCode    = (uint32_t*)shaderContents.data();
  VK_CHECK_LOG_THROW(vkCreateShaderModule(renderContext.vkDevice, &shaderModuleCI, nullptr, &pddit->second.shaderModule), "Cannot create shader module : " << fileName);
}

VkShaderModule ShaderModule::getHandle(VkDevice device) const
{
  auto pddit = perDeviceData.find(device);
  if (pddit == end(perDeviceData))
    return VK_NULL_HANDLE;
  return pddit->second.shaderModule;
}

Pipeline::Pipeline(std::shared_ptr<PipelineCache> pc, std::shared_ptr<PipelineLayout> pl)
  : pipelineCache{ pc }, pipelineLayout{ pl }
{
}

Pipeline::~Pipeline()
{
}

VkPipeline Pipeline::getHandlePipeline(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto keyValue = getKeyID(renderContext, pbPerDevice);
  auto pddit = perDeviceData.find(keyValue);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.data[renderContext.activeIndex % activeCount].pipeline;
}

VertexInputDefinition::VertexInputDefinition(uint32_t b, VkVertexInputRate ir, const std::vector<VertexSemantic>& s)
  : binding{ b }, inputRate{ ir }, semantic(s)
{
}

BlendAttachmentDefinition::BlendAttachmentDefinition(VkBool32 en, VkColorComponentFlags mask, VkBlendFactor srcC, VkBlendFactor dstC, VkBlendOp opC, VkBlendFactor srcA, VkBlendFactor dstA, VkBlendOp opA)
  : blendEnable{ en }, colorWriteMask{ mask }, srcColorBlendFactor{ srcC }, dstColorBlendFactor{ dstC }, colorBlendOp{ opC }, srcAlphaBlendFactor{ srcA }, dstAlphaBlendFactor{ dstA }, alphaBlendOp{ opA }
{
}

ShaderStageDefinition::ShaderStageDefinition()
  : stage{ VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM }, shaderModule(), entryPoint("main")
{
}

ShaderStageDefinition::ShaderStageDefinition(VkShaderStageFlagBits s, std::shared_ptr<ShaderModule> sm, const std::string& ep)
  : stage{ s }, shaderModule(sm), entryPoint(ep)
{
}

GraphicsPipeline::GraphicsPipeline(std::shared_ptr<PipelineCache> pc, std::shared_ptr<PipelineLayout> pl)
  : Pipeline{ pc, pl }
{
  front.failOp      = VK_STENCIL_OP_KEEP;
  front.passOp      = VK_STENCIL_OP_KEEP;
  front.depthFailOp = VK_STENCIL_OP_KEEP;
  front.compareOp   = VK_COMPARE_OP_ALWAYS;
  front.compareMask = 0;
  front.writeMask   = 0;
  front.reference   = 0;

  back.failOp       = VK_STENCIL_OP_KEEP;
  back.passOp       = VK_STENCIL_OP_KEEP;
  back.depthFailOp  = VK_STENCIL_OP_KEEP;
  back.compareOp    = VK_COMPARE_OP_ALWAYS;
  back.compareMask  = 0;
  back.writeMask    = 0;
  back.reference    = 0;
}

GraphicsPipeline::~GraphicsPipeline()
{
  shaderStages.clear();

  for (auto& pdd : perDeviceData)
    for (uint32_t i = 0; i<pdd.second.data.size(); ++i)
      vkDestroyPipeline(pdd.second.device, pdd.second.data[i].pipeline, nullptr);
}

void GraphicsPipeline::accept(NodeVisitor& visitor)
{
  if (visitor.getMask() && mask)
  {
    visitor.push(this);
    visitor.apply(*this);
    visitor.pop();
  }
}

void GraphicsPipeline::validate(const RenderContext& renderContext)
{
  pipelineCache->validate(renderContext);
  pipelineLayout->validate(renderContext);

  if (renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perDeviceData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKeyID(renderContext, pbPerDevice);
  auto pddit = perDeviceData.find(keyValue);
  if (pddit == cend(perDeviceData))
    pddit = perDeviceData.insert({ keyValue, PipelineData(renderContext, swForEachImage) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  if (pddit->second.data[activeIndex].pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(pddit->second.device, pddit->second.data[activeIndex].pipeline, nullptr);
    pddit->second.data[activeIndex].pipeline = VK_NULL_HANDLE;
  }

  std::vector<VkPipelineShaderStageCreateInfo>   shaderStagesCI;
  std::vector<VkVertexInputBindingDescription>   bindingDescriptions;
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

  for (const auto& state : shaderStages)
  {
    state.shaderModule->validate(renderContext);
    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType                          = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage                          = state.stage;
    shaderStage.module                         = state.shaderModule->getHandle(renderContext.vkDevice);
    shaderStage.pName                          = state.entryPoint.c_str();//"main";
    shaderStagesCI.push_back(shaderStage);
  }

  for (const auto& state : vertexInput)
  {
    VkVertexInputBindingDescription inputDescription{};
      inputDescription.binding                 = state.binding;
      inputDescription.stride                  = calcVertexSize(state.semantic) * sizeof(float);
      inputDescription.inputRate               = state.inputRate;
    bindingDescriptions.emplace_back(inputDescription);

    uint32_t attribOffset   = 0;
    uint32_t attribLocation = 0;
    for (const auto& attrib : state.semantic)
    {
      uint32_t attribSize = attrib.size * sizeof(float);
      VkVertexInputAttributeDescription inputAttribDescription{};
        inputAttribDescription.location        = attribLocation++;
        inputAttribDescription.binding         = state.binding;
        inputAttribDescription.format          = attrib.getVertexFormat();
        inputAttribDescription.offset          = attribOffset;
        attributeDescriptions.emplace_back(inputAttribDescription);
      attribOffset += attribSize;
    }
  }
  VkPipelineVertexInputStateCreateInfo inputState{};
    inputState.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    inputState.vertexBindingDescriptionCount   = bindingDescriptions.size();
    inputState.pVertexBindingDescriptions      = bindingDescriptions.data();
    inputState.vertexAttributeDescriptionCount = attributeDescriptions.size();
    inputState.pVertexAttributeDescriptions    = attributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
    inputAssemblyState.sType                   = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyState.topology                = topology;
    inputAssemblyState.primitiveRestartEnable  = primitiveRestartEnable;

  VkPipelineTessellationStateCreateInfo tesselationState{};
    tesselationState.sType                       = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tesselationState.patchControlPoints          = patchControlPoints;

  VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    if (hasDynamicState(VK_DYNAMIC_STATE_VIEWPORT))
    {
      viewportState.pViewports                 = nullptr;
      viewportState.viewportCount              = 1; // FIXME - really ?!?
    }
    else
    {
      viewportState.pViewports                 = viewports.data();
      viewportState.viewportCount              = viewports.size();
    }
    if (hasDynamicState(VK_DYNAMIC_STATE_SCISSOR))
    {
      viewportState.pScissors                  = nullptr;
      viewportState.scissorCount               = 1; // FIXME - really ?!?
    }
    else
    {
      viewportState.pScissors                  = scissors.data();
      viewportState.scissorCount               = scissors.size();
    }

  VkPipelineRasterizationStateCreateInfo rasterizationState{};
    rasterizationState.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationState.depthClampEnable        = depthClampEnable;
    rasterizationState.rasterizerDiscardEnable = rasterizerDiscardEnable;
    rasterizationState.polygonMode             = polygonMode;
    rasterizationState.cullMode                = cullMode;
    rasterizationState.frontFace               = frontFace;
    rasterizationState.depthBiasEnable         = depthBiasEnable;
    rasterizationState.depthBiasConstantFactor = depthBiasConstantFactor;
    rasterizationState.depthBiasClamp          = depthBiasClamp;
    rasterizationState.depthBiasSlopeFactor    = depthBiasSlopeFactor;
    rasterizationState.lineWidth               = lineWidth;

  VkPipelineMultisampleStateCreateInfo multisampleState{};
    multisampleState.sType                     = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleState.rasterizationSamples      = rasterizationSamples;
    multisampleState.sampleShadingEnable       = sampleShadingEnable;
    multisampleState.minSampleShading          = minSampleShading;
    multisampleState.pSampleMask               = pSampleMask; // FIXME : a pointer ?
    multisampleState.alphaToCoverageEnable     = alphaToCoverageEnable;
    multisampleState.alphaToOneEnable          = alphaToOneEnable;

  VkPipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.sType                    = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilState.depthTestEnable          = depthTestEnable;
    depthStencilState.depthWriteEnable         = depthWriteEnable;
    depthStencilState.depthCompareOp           = depthCompareOp;
    depthStencilState.depthBoundsTestEnable    = depthBoundsTestEnable;
    depthStencilState.stencilTestEnable        = stencilTestEnable;
    depthStencilState.front.failOp             = front.failOp;
    depthStencilState.front.passOp             = front.passOp;
    depthStencilState.front.depthFailOp        = front.depthFailOp;
    depthStencilState.front.compareOp          = front.compareOp;
    depthStencilState.front.compareMask        = front.compareMask;
    depthStencilState.front.writeMask          = front.writeMask;
    depthStencilState.front.reference          = front.reference;
    depthStencilState.back.failOp              = back.failOp;
    depthStencilState.back.passOp              = back.passOp;
    depthStencilState.back.depthFailOp         = back.depthFailOp;
    depthStencilState.back.compareOp           = back.compareOp;
    depthStencilState.back.compareMask         = back.compareMask;
    depthStencilState.back.writeMask           = back.writeMask;
    depthStencilState.back.reference           = back.reference;
    depthStencilState.minDepthBounds           = minDepthBounds;
    depthStencilState.maxDepthBounds           = maxDepthBounds;

  std::vector<VkPipelineColorBlendAttachmentState> vkBlendAttachments;
  for (const auto& b : blendAttachments)
  {
    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.blendEnable           = b.blendEnable;
    blendAttachmentState.srcColorBlendFactor   = b.srcColorBlendFactor;
    blendAttachmentState.dstColorBlendFactor   = b.dstColorBlendFactor;
    blendAttachmentState.colorBlendOp          = b.colorBlendOp;
    blendAttachmentState.srcAlphaBlendFactor   = b.srcAlphaBlendFactor;
    blendAttachmentState.dstAlphaBlendFactor   = b.dstAlphaBlendFactor;
    blendAttachmentState.alphaBlendOp          = b.alphaBlendOp;
    blendAttachmentState.colorWriteMask        = b.colorWriteMask;
    vkBlendAttachments.emplace_back(blendAttachmentState);
  }

  VkPipelineColorBlendStateCreateInfo colorBlendState{};
    colorBlendState.sType                      = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendState.attachmentCount            = vkBlendAttachments.size();
    colorBlendState.pAttachments               = vkBlendAttachments.data();

  VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType                         = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pDynamicStates                = dynamicStates.data();
    dynamicState.dynamicStateCount             = dynamicStates.size();

  pipelineLayout->validate(renderContext);
  VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType                           = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.layout                          = pipelineLayout->getHandle(renderContext.vkDevice);
    pipelineCI.renderPass                      = renderContext.renderPass->getHandle(renderContext);
    pipelineCI.subpass                         = renderContext.subpassIndex;
    pipelineCI.stageCount                      = shaderStagesCI.size();
    pipelineCI.pStages                         = shaderStagesCI.data();
    pipelineCI.pVertexInputState               = &inputState;
    pipelineCI.pInputAssemblyState             = &inputAssemblyState;
    if (hasShaderStage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) || hasShaderStage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))
      pipelineCI.pTessellationState              = &tesselationState;
    pipelineCI.pRasterizationState             = &rasterizationState;
    pipelineCI.pColorBlendState                = &colorBlendState;
    pipelineCI.pMultisampleState               = &multisampleState;
    pipelineCI.pViewportState                  = &viewportState;
    pipelineCI.pDepthStencilState              = &depthStencilState;
    pipelineCI.pDynamicState                   = &dynamicState;
  VK_CHECK_LOG_THROW(vkCreateGraphicsPipelines(pddit->second.device, pipelineCache->getHandle(pddit->second.device), 1, &pipelineCI, nullptr, &pddit->second.data[activeIndex].pipeline), "Cannot create graphics pipeline");
  pddit->second.valid[activeIndex] = true;
  // we have a new graphics pipeline so we must invalidate command buffers
  notifyCommandBuffers();
}

ComputePipeline::ComputePipeline(std::shared_ptr<PipelineCache> pc, std::shared_ptr<PipelineLayout> pl)
  : Pipeline{ pc, pl }
{
}

ComputePipeline::~ComputePipeline()
{
  for (auto& pdd : perDeviceData)
    for (uint32_t i = 0; i<pdd.second.data.size(); ++i)
      vkDestroyPipeline(pdd.second.device, pdd.second.data[i].pipeline, nullptr);
}

void ComputePipeline::accept(NodeVisitor& visitor)
{
  if (visitor.getMask() && mask)
  {
    visitor.push(this);
    visitor.apply(*this);
    visitor.pop();
  }
}

void ComputePipeline::validate(const RenderContext& renderContext)
{
  pipelineCache->validate(renderContext);
  pipelineLayout->validate(renderContext);

  if (renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perDeviceData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKeyID(renderContext, pbPerDevice);
  auto pddit = perDeviceData.find(keyValue);
  if (pddit == cend(perDeviceData))
    pddit = perDeviceData.insert({ keyValue, PipelineData(renderContext, swForEachImage) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  if (pddit->second.data[activeIndex].pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(pddit->second.device, pddit->second.data[activeIndex].pipeline, nullptr);
    pddit->second.data[activeIndex].pipeline = VK_NULL_HANDLE;
  }

  shaderStage.shaderModule->validate(renderContext);
  VkComputePipelineCreateInfo pipelineCI{};
    pipelineCI.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCI.layout       = pipelineLayout->getHandle(renderContext.vkDevice);
    pipelineCI.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineCI.stage.stage  = shaderStage.stage;
    pipelineCI.stage.module = shaderStage.shaderModule->getHandle(renderContext.vkDevice);
    pipelineCI.stage.pName  = shaderStage.entryPoint.c_str();
  VK_CHECK_LOG_THROW(vkCreateComputePipelines(pddit->second.device, pipelineCache->getHandle(pddit->second.device), 1, &pipelineCI, nullptr, &pddit->second.data[activeIndex].pipeline), "Cannot create compute pipeline");
  pddit->second.valid[activeIndex] = true;
  // we have a new compute pipeline so we must invalidate command buffers
  notifyCommandBuffers();
}

