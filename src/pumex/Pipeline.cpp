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

#include <pumex/Pipeline.h>
#include <pumex/RenderPass.h>
#include <pumex/utils/Log.h>
#include <pumex/Device.h>
#include <pumex/Surface.h>
#include <fstream>

using namespace pumex;

DescriptorSetLayoutBinding::DescriptorSetLayoutBinding(uint32_t b, uint32_t bc, VkDescriptorType dt, VkShaderStageFlags sf)
  : binding{ b }, bindingCount{ bc }, descriptorType{ dt }, stageFlags{ sf }
{
}


VertexInputDefinition::VertexInputDefinition(uint32_t b, VkVertexInputRate ir, const std::vector<VertexSemantic>& s)
  : binding{ b }, inputRate{ ir }, semantic(s)
{
}

BlendAttachmentDefinition::BlendAttachmentDefinition(VkBool32 en, VkColorComponentFlags mask, VkBlendFactor srcC, VkBlendFactor dstC, VkBlendOp opC, VkBlendFactor srcA, VkBlendFactor dstA, VkBlendOp opA)
  : blendEnable{ en }, colorWriteMask{ mask }, srcColorBlendFactor{ srcC }, dstColorBlendFactor{ dstC }, colorBlendOp{ opC }, srcAlphaBlendFactor{ srcA }, dstAlphaBlendFactor{ dstA }, alphaBlendOp{opA}
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


DescriptorSetLayout::DescriptorSetLayout(const std::vector<DescriptorSetLayoutBinding>& b)
  : bindings(b)
{
}

DescriptorSetLayout::~DescriptorSetLayout()
{
  for ( auto& pddit : perDeviceData)
    vkDestroyDescriptorSetLayout( pddit.first, pddit.second.descriptorSetLayout, nullptr);
}


void DescriptorSetLayout::validate(Device* device)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit != perDeviceData.end())
    return;
  pddit = perDeviceData.insert( {device->device, PerDeviceData()} ).first;

  std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
  for ( const auto& b : bindings )
  {
    VkDescriptorSetLayoutBinding setLayoutBinding{};
      setLayoutBinding.descriptorType  = b.descriptorType;
      setLayoutBinding.stageFlags      = b.stageFlags;
      setLayoutBinding.binding         = b.binding;
      setLayoutBinding.descriptorCount = b.bindingCount;
    setLayoutBindings.push_back(setLayoutBinding);
  }

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
    descriptorSetLayoutCI.bindingCount = setLayoutBindings.size();
  VK_CHECK_LOG_THROW(vkCreateDescriptorSetLayout(pddit->first, &descriptorSetLayoutCI, nullptr, &pddit->second.descriptorSetLayout), "Cannot create descriptor set layout");
}

VkDescriptorSetLayout DescriptorSetLayout::getHandle(VkDevice device) const
{
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.descriptorSetLayout;
}

VkDescriptorType DescriptorSetLayout::getDescriptorType(uint32_t binding) const
{
  for (const auto& b : bindings)
  {
    if (binding == b.binding)
      return b.descriptorType;
  }
  return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

uint32_t DescriptorSetLayout::getDescriptorBindingCount(uint32_t binding) const
{
  for (const auto& b : bindings)
  {
    if (binding == b.binding)
      return b.bindingCount;
  }
  return 0;
}


DescriptorPool::DescriptorPool(uint32_t ps, const std::vector<DescriptorSetLayoutBinding>& b)
  : poolSize(ps), bindings(b)
{
}

DescriptorPool::~DescriptorPool()
{
  for (auto& pddit : perDeviceData)
    vkDestroyDescriptorPool(pddit.first, pddit.second.descriptorPool, nullptr);
}


void DescriptorPool::validate(Device* device)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  if (pddit != perDeviceData.end())
    return;
  pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;

  std::vector<VkDescriptorPoolSize> poolSizes;
  for (const auto& b : bindings)
  {
    VkDescriptorPoolSize descriptorPoolSize{};
    descriptorPoolSize.type = b.descriptorType;
    descriptorPoolSize.descriptorCount = b.bindingCount * poolSize;
    poolSizes.push_back(descriptorPoolSize);
  }
  VkDescriptorPoolCreateInfo descriptorPoolCI{};
    descriptorPoolCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.poolSizeCount = poolSizes.size();
    descriptorPoolCI.pPoolSizes    = poolSizes.data();
    descriptorPoolCI.maxSets       = poolSize;
    descriptorPoolCI.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // we will free our desriptor sets manually
  VK_CHECK_LOG_THROW(vkCreateDescriptorPool(pddit->first, &descriptorPoolCI, nullptr, &pddit->second.descriptorPool), "Cannot create descriptor pool");
}

VkDescriptorPool DescriptorPool::getHandle(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.descriptorPool;
}

DescriptorSetValue::DescriptorSetValue()
  : vType{ Undefined }
{
}

DescriptorSetValue::DescriptorSetValue(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
  : vType{ Buffer }
{
  bufferInfo.buffer = buffer;
  bufferInfo.offset = offset;
  bufferInfo.range  = range;
}

DescriptorSetValue::DescriptorSetValue(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
  : vType{ Image }
{
  imageInfo.sampler     = sampler;
  imageInfo.imageView   = imageView;
  imageInfo.imageLayout = imageLayout;
}

DescriptorSetSource::~DescriptorSetSource()
{
}

void DescriptorSetSource::addDescriptorSet(DescriptorSet* descriptorSet)
{
  descriptorSets.insert(descriptorSet);
}

void DescriptorSetSource::removeDescriptorSet(DescriptorSet* descriptorSet)
{
  descriptorSets.erase(descriptorSet);
}

void DescriptorSetSource::notifyDescriptorSets()
{
  for ( auto ds : descriptorSets )
    ds->setDirty();
}

//void DescriptorSetSourceArray::addSource(std::shared_ptr<DescriptorSetSource> source)
//{
//  sources.push_back(source);
//}
//
//void DescriptorSetSourceArray::addDescriptorSet(DescriptorSet* descriptorSet)
//{
//  for (auto& s : sources)
//  {
//    s->addDescriptorSet(descriptorSet);
//  }
//}
//
//void DescriptorSetSourceArray::removeDescriptorSet(DescriptorSet* descriptorSet)
//{
//  for (auto& s : sources)
//  {
//    s->removeDescriptorSet(descriptorSet);
//  }
//}
//
//void DescriptorSetSourceArray::getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const
//{
//  for (const auto& s : sources)
//  {
//    s->getDescriptorSetValues(device, index, values);
//  }
//}
//
//void DescriptorSetSourceArray::getDescriptorSetValues(VkSurfaceKHR surface, uint32_t index, std::vector<DescriptorSetValue>& values) const
//{
//  for (const auto& s : sources)
//  {
//    s->getDescriptorSetValues(surface, index, values);
//  }
//}
//
//void DescriptorSetSourceArray::notifyDescriptorSets()
//{
//  for (auto& s : sources)
//    s->notifyDescriptorSets();
//}

DescriptorSet::DescriptorSet(std::shared_ptr<DescriptorSetLayout> l, std::shared_ptr<DescriptorPool> p, uint32_t ac)
  : layout{ l }, pool{ p }, activeCount{ ac }
{
}

DescriptorSet::~DescriptorSet()
{
  for ( auto s : sources)
    s.second->removeDescriptorSet(this);
  sources.clear();

  for (auto& pddit : perSurfaceData)
    vkFreeDescriptorSets(pddit.second.device, pool->getHandle(pddit.second.device), activeCount, pddit.second.descriptorSet.data());
}


void DescriptorSet::validate(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(activeCount,surface->device.lock()->device) }).first;
  if (!pddit->second.dirty[activeIndex])
    return;
  if (pddit->second.descriptorSet[activeIndex] == VK_NULL_HANDLE)
  {
    VkDescriptorSetLayout layoutHandle = layout->getHandle(pddit->second.device);

    VkDescriptorSetAllocateInfo descriptorSetAinfo{};
      descriptorSetAinfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      descriptorSetAinfo.descriptorPool     = pool->getHandle(pddit->second.device);
      descriptorSetAinfo.descriptorSetCount = 1;
      descriptorSetAinfo.pSetLayouts        = &layoutHandle;
    VK_CHECK_LOG_THROW(vkAllocateDescriptorSets(pddit->second.device, &descriptorSetAinfo, &pddit->second.descriptorSet[activeIndex]), "Cannot allocate descriptor sets");
  }

  std::map<uint32_t, std::vector<DescriptorSetValue>> values;
  uint32_t dsvSize = 0;
  for (const auto& s : sources)
  {
    std::vector<DescriptorSetValue> value;
    // get descriptor set values based on device
    s.second->getDescriptorSetValues(pddit->second.device,activeIndex, value);
    // get descriptor set values based on surface ( input attachments )
    s.second->getDescriptorSetValues(pddit->first, activeIndex, value);
    dsvSize += layout->getDescriptorBindingCount(s.first);
    values.insert({ s.first, value });
  }
  std::vector<VkWriteDescriptorSet> writeDescriptorSets;
  std::vector<VkDescriptorBufferInfo> bufferInfos(dsvSize);
  std::vector<VkDescriptorImageInfo>  imageInfos(dsvSize);
  uint32_t bufferInfosCurrentSize = 0;
  uint32_t imageInfosCurrentSize  = 0;
  for (const auto& v : values)
  {
    if (v.second.empty())
      continue;
    VkWriteDescriptorSet writeDescriptorSet{};
      writeDescriptorSet.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDescriptorSet.dstSet          = pddit->second.descriptorSet[activeIndex];
      writeDescriptorSet.descriptorType  = layout->getDescriptorType(v.first);
      writeDescriptorSet.dstBinding      = v.first;
      writeDescriptorSet.descriptorCount = layout->getDescriptorBindingCount(v.first);
      switch (v.second[0].vType)
      {
      case DescriptorSetValue::Buffer:
        writeDescriptorSet.pBufferInfo = &bufferInfos[bufferInfosCurrentSize];
        for (const auto& dsv : v.second)
          bufferInfos[bufferInfosCurrentSize++] = dsv.bufferInfo;
        break;
      case DescriptorSetValue::Image:
        writeDescriptorSet.pImageInfo = &imageInfos[imageInfosCurrentSize];
        for (const auto& dsv : v.second)
          imageInfos[imageInfosCurrentSize++] = dsv.imageInfo;
        for(uint32_t i=v.second.size(); i<layout->getDescriptorBindingCount(v.first); ++i)
          imageInfos[imageInfosCurrentSize++] = v.second[0].imageInfo;

        break;
      default:
        continue;
      }
    writeDescriptorSets.push_back(writeDescriptorSet);
  }
  vkUpdateDescriptorSets(pddit->second.device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
  notifyCommandBuffers(activeIndex);
  pddit->second.dirty[activeIndex] = false;
}

VkDescriptorSet DescriptorSet::getHandle(VkSurfaceKHR surface) const
{
  auto pddit = perSurfaceData.find(surface);
  if (pddit == perSurfaceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.descriptorSet[activeIndex];
}

void DescriptorSet::setDirty()
{
  for (auto& pdd : perSurfaceData)
    for(auto&& d : pdd.second.dirty)
      d = true;
}

void DescriptorSet::setSource(uint32_t binding, std::shared_ptr<DescriptorSetSource> source)
{
  resetSource(binding);
  sources[binding] = source;
  source->addDescriptorSet(this);
  setDirty();
}

void DescriptorSet::resetSource(uint32_t binding)
{
  auto it = sources.find(binding);
  if (it != sources.end())
  {
    it->second->removeDescriptorSet(this);
    sources.erase(binding);
  }
}


PipelineLayout::PipelineLayout()
{
}

PipelineLayout::~PipelineLayout()
{
  for (auto& pddit : perDeviceData)
    vkDestroyPipelineLayout(pddit.first, pddit.second.pipelineLayout, nullptr);
}


void PipelineLayout::validate(Device* device)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  if (pddit != perDeviceData.end())
    return;
  pddit = perDeviceData.insert( {device->device, PerDeviceData()}).first;

  VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    std::vector<VkDescriptorSetLayout> descriptors;
    for ( auto dsl : descriptorSetLayouts )
    { 
      dsl->validate(device);
      descriptors.push_back(dsl->getHandle(device->device));
    }
    pipelineLayoutCI.setLayoutCount = descriptors.size();
    pipelineLayoutCI.pSetLayouts    = descriptors.data();
  VK_CHECK_LOG_THROW(vkCreatePipelineLayout(pddit->first, &pipelineLayoutCI, nullptr, &pddit->second.pipelineLayout), "Cannot create pipeline layout");
}

VkPipelineLayout PipelineLayout::getHandle(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
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


void PipelineCache::validate(Device* device)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  if (pddit != perDeviceData.end())
    return;
  pddit = perDeviceData.insert({device->device,PerDeviceData()}).first;

  VkPipelineCacheCreateInfo pipelineCacheCI{};
    pipelineCacheCI.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  VK_CHECK_LOG_THROW(vkCreatePipelineCache(pddit->first, &pipelineCacheCI, nullptr, &pddit->second.pipelineCache), "Cannot create pipeline cache");
}

VkPipelineCache PipelineCache::getHandle(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
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


void ShaderModule::validate(Device* device)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit != perDeviceData.end())
    return;
  pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;

  VkShaderModuleCreateInfo shaderModuleCI{};
    shaderModuleCI.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCI.codeSize = shaderContents.size();
    shaderModuleCI.pCode    = (uint32_t*)shaderContents.data();
  VK_CHECK_LOG_THROW(vkCreateShaderModule(device->device, &shaderModuleCI, nullptr, &pddit->second.shaderModule), "Cannot create shader module : " << fileName);
}

VkShaderModule ShaderModule::getHandle(VkDevice device) const
{
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.shaderModule;
}

GraphicsPipeline::GraphicsPipeline(std::shared_ptr<PipelineCache> pc, std::shared_ptr<PipelineLayout> pl, std::shared_ptr<RenderPass> rp, uint32_t s)
  : pipelineCache{ pc }, pipelineLayout{ pl }, renderPass{ rp }, subpass{s}
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
  for (auto& pddit : perDeviceData)
    vkDestroyPipeline(pddit.first, pddit.second.pipeline, nullptr);
}


void GraphicsPipeline::validate(Device* device)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (!pddit->second.dirty)
    return;
  if (pddit->second.pipeline != VK_NULL_HANDLE)
    vkDestroyPipeline(pddit->first, pddit->second.pipeline, nullptr);

  std::vector<VkPipelineShaderStageCreateInfo>   shaderStagesCI;
  std::vector<VkVertexInputBindingDescription>   bindingDescriptions;
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

  for (const auto& state : shaderStages)
  {
    state.shaderModule->validate(device);
    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType                          = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage                          = state.stage;
    shaderStage.module                         = state.shaderModule->getHandle(device->device);
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

  renderPass->validate(device);
  pipelineLayout->validate(device);
  VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType                           = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.layout                          = pipelineLayout->getHandle(device->device);
    pipelineCI.renderPass                      = renderPass->getHandle(device->device);
    pipelineCI.subpass                         = subpass;
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
  VK_CHECK_LOG_THROW(vkCreateGraphicsPipelines(pddit->first, pipelineCache->getHandle(device->device), 1, &pipelineCI, nullptr, &pddit->second.pipeline), "Cannot create graphics pipeline");
  notifyCommandBuffers();
  pddit->second.dirty = false;
}

VkPipeline GraphicsPipeline::getHandle(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.pipeline;
}

void GraphicsPipeline::setDirty()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    pdd.second.dirty = true;
}

ComputePipeline::ComputePipeline(std::shared_ptr<PipelineCache> pc, std::shared_ptr<PipelineLayout> pl)
  : pipelineCache{ pc }, pipelineLayout{ pl }
{
}

ComputePipeline::~ComputePipeline()
{
  for (auto& pddit : perDeviceData)
    vkDestroyPipeline(pddit.first, pddit.second.pipeline, nullptr);
}


void ComputePipeline::validate(Device* device)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (!pddit->second.dirty)
    return;
  if (pddit->second.pipeline != VK_NULL_HANDLE)
    vkDestroyPipeline(pddit->first, pddit->second.pipeline, nullptr);

  shaderStage.shaderModule->validate(device);
  pipelineLayout->validate(device);
  VkComputePipelineCreateInfo pipelineCI{};
    pipelineCI.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCI.layout       = pipelineLayout->getHandle(device->device);
    pipelineCI.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineCI.stage.stage  = shaderStage.stage;
    pipelineCI.stage.module = shaderStage.shaderModule->getHandle(device->device);
    pipelineCI.stage.pName  = shaderStage.entryPoint.c_str();//"main";

  VK_CHECK_LOG_THROW(vkCreateComputePipelines(pddit->first, pipelineCache->getHandle(device->device), 1, &pipelineCI, nullptr, &pddit->second.pipeline), "Cannot create compute pipeline");
  notifyCommandBuffers();
  pddit->second.dirty = false;
}

VkPipeline ComputePipeline::getHandle(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.pipeline;
}

void ComputePipeline::setDirty()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    pdd.second.dirty = true;
}

