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

#include <pumex/Descriptor.h>
#include <map>
#include <algorithm>
#include <pumex/Device.h>
#include <pumex/Surface.h>
#include <pumex/Viewer.h>
#include <pumex/Node.h>
#include <pumex/RenderContext.h>
#include <pumex/utils/HashCombine.h>
#include <pumex/utils/Log.h>

using namespace pumex;

DescriptorSetLayoutBinding::DescriptorSetLayoutBinding(uint32_t b, uint32_t bc, VkDescriptorType dt, VkShaderStageFlags sf)
  : binding{ b }, bindingCount{ bc }, descriptorType{ dt }, stageFlags{ sf }
{
}

namespace pumex
{
std::size_t computeHash(const std::vector<DescriptorSetLayoutBinding> layoutBindings)
{
  std::size_t seed = 0;
  for (auto& v : layoutBindings)
  {
    std::size_t a = hash_value(v.binding, v.bindingCount, v.descriptorType, v.stageFlags);
    seed ^= a + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  }
  return seed;
}
}

DescriptorPool::DescriptorPool()
{
}

DescriptorPool::~DescriptorPool()
{
  for (auto& pddit : perObjectData)
    for(uint32_t i=0; i<pddit.second.data.size(); ++i)
      for( auto& descriptorPool : pddit.second.data[i].descriptorPools)
        vkDestroyDescriptorPool(pddit.second.device, descriptorPool, nullptr);
}

  uint32_t DescriptorPool::registerDescriptorSet(std::shared_ptr<DescriptorSetLayout> layout)
  {
    std::lock_guard<std::mutex> lock(mutex);
    // find pool that uses the same layout and has not been validated yet
    auto hashVal = layout->getHashValue();
    auto it = std::find_if(begin(poolDefinitions), end(poolDefinitions), [&](const SinglePoolDefinition& pd) { return (pd.maxSets == 0) && (pd.layout->getHashValue() == hashVal); });
    uint32_t index;
    if (it == end(poolDefinitions))
    {
      index = poolDefinitions.size();
      poolDefinitions.push_back(SinglePoolDefinition(layout));
    }
    else
    {
      index = std::distance(begin(poolDefinitions), it);
      it->registeredDescriptorSets++;
    }
    return index;
  }

  VkDescriptorSet DescriptorPool::allocate(const RenderContext& renderContext, uint32_t index)
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto keyValue = getKeyID(renderContext, pbPerDevice);
    auto pddit    = perObjectData.find(keyValue);
    if (pddit == end(perObjectData))
      pddit = perObjectData.insert({ keyValue, DescriptorPoolData(renderContext, swOnce) }).first;

    if (poolDefinitions[index].maxSets == 0)
    {
      uint32_t poolSize = poolDefinitions[index].registeredDescriptorSets * renderContext.imageCount * renderContext.surface->viewer.lock()->getNumSurfaces();
      std::vector<VkDescriptorPoolSize> poolSizes = poolDefinitions[index].layout->getDescriptorPoolSize(poolSize);
      VkDescriptorPoolCreateInfo descriptorPoolCI{};
        descriptorPoolCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCI.poolSizeCount = poolSizes.size();
        descriptorPoolCI.pPoolSizes    = poolSizes.data();
        descriptorPoolCI.maxSets       = poolSize;
        descriptorPoolCI.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // we will free our descriptor sets manually
        pddit->second.data[0].descriptorPools.resize(poolDefinitions.size(), VK_NULL_HANDLE);
        pddit->second.data[0].allocatedDescriptors.resize(poolDefinitions.size(), 0);
        VK_CHECK_LOG_THROW(vkCreateDescriptorPool(pddit->second.device, &descriptorPoolCI, nullptr, &pddit->second.data[0].descriptorPools[index]), "Cannot create descriptor pool");
        poolDefinitions[index].maxSets = poolSize;
    }
    CHECK_LOG_THROW(pddit->second.data[0].allocatedDescriptors[index] >= poolDefinitions[index].maxSets, "Cannot allocate another descriptor set. Descriptor pool is full");

    VkDescriptorSetLayout layoutHandle = poolDefinitions[index].layout->getHandle(renderContext);
    VkDescriptorSetAllocateInfo descriptorSetAinfo{};
      descriptorSetAinfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      descriptorSetAinfo.descriptorPool     = pddit->second.data[0].descriptorPools[index];
      descriptorSetAinfo.descriptorSetCount = 1;
      descriptorSetAinfo.pSetLayouts        = &layoutHandle;
    VkDescriptorSet descriptorSet;
    VK_CHECK_LOG_THROW(vkAllocateDescriptorSets(pddit->second.device, &descriptorSetAinfo, &descriptorSet), "Cannot allocate descriptor set");
    pddit->second.data[0].allocatedDescriptors[index]++;
    return descriptorSet;
  }

  void DescriptorPool::deallocate(uint32_t deviceID, uint32_t index, VkDescriptorSet descriptorSet)
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto pddit    = perObjectData.find(deviceID);
    if (pddit == end(perObjectData))
      return;
    CHECK_LOG_THROW(poolDefinitions[index].maxSets == 0, "Cannot deallocate descriptor set - descriptor pool was not created before");
    vkFreeDescriptorSets(pddit->second.device, pddit->second.data[0].descriptorPools[index], 1, &descriptorSet);
    pddit->second.data[0].allocatedDescriptors[index]--;
  }

DescriptorSetLayout::DescriptorSetLayout(const std::vector<DescriptorSetLayoutBinding>& b)
  : bindings(b)
{
  hashValue = computeHash(bindings);
}

DescriptorSetLayout::~DescriptorSetLayout()
{
  std::lock_guard<std::mutex> lock(mutex);
  for ( auto& pddit : perObjectData )
    for (uint32_t i = 0; i<pddit.second.data.size(); ++i)
      vkDestroyDescriptorSetLayout( pddit.second.device, pddit.second.data[i].descriptorSetLayout, nullptr);
}

void DescriptorSetLayout::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto keyValue = getKeyID(renderContext, pbPerDevice);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, DescriptorSetLayoutData(renderContext, swOnce) }).first;
  if (pddit->second.valid[0])
    return;

  std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
  for ( const auto& b : bindings )
  {
    VkDescriptorSetLayoutBinding setLayoutBinding{};
      setLayoutBinding.descriptorType  = b.descriptorType;
      setLayoutBinding.stageFlags      = b.stageFlags;
      setLayoutBinding.binding         = b.binding;
      setLayoutBinding.descriptorCount = b.bindingCount;
    setLayoutBindings.emplace_back(setLayoutBinding);
  }

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCI.pBindings    = setLayoutBindings.data();
    descriptorSetLayoutCI.bindingCount = setLayoutBindings.size();
  VK_CHECK_LOG_THROW(vkCreateDescriptorSetLayout(pddit->second.device, &descriptorSetLayoutCI, nullptr, &pddit->second.data[0].descriptorSetLayout), "Cannot create descriptor set layout");
  pddit->second.valid[0] = true;
}

VkDescriptorSetLayout DescriptorSetLayout::getHandle(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto keyValue = getKeyID(renderContext, pbPerDevice);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    return VK_NULL_HANDLE;
  return pddit->second.data[0].descriptorSetLayout;
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

std::vector<VkDescriptorPoolSize> DescriptorSetLayout::getDescriptorPoolSize(uint32_t poolSize) const
{
  std::vector<VkDescriptorPoolSize> poolSizes;
  for (const auto& b : bindings)
  {
    VkDescriptorPoolSize descriptorPoolSize{};
    descriptorPoolSize.type            = b.descriptorType;
    descriptorPoolSize.descriptorCount = b.bindingCount * poolSize;
    poolSizes.push_back(descriptorPoolSize);
  }
  return std::move(poolSizes);
}

Descriptor::Descriptor(std::shared_ptr<DescriptorSet> o, std::shared_ptr<Resource> r, VkDescriptorType dt)
  : owner{ o }, descriptorType{ dt }
{
  resources.push_back(r);
}

Descriptor::Descriptor(std::shared_ptr<DescriptorSet> o, const std::vector<std::shared_ptr<Resource>>& r, VkDescriptorType dt)
  : owner{ o }, descriptorType{ dt }
{
  resources = r;
}

void Descriptor::registerInResources()
{
  for (auto& res : resources)
    res->addDescriptor(shared_from_this());
}

void Descriptor::unregisterFromResources()
{
  for (auto& res : resources)
    res->removeDescriptor(shared_from_this());
}

Descriptor::~Descriptor()
{
}

void Descriptor::validate(const RenderContext& renderContext)
{
  for (auto& res : resources)
    res->validate(renderContext);
}

void Descriptor::invalidateDescriptorSet()
{
  owner.lock()->invalidateOwners();
}

void Descriptor::notifyDescriptorSet(const RenderContext& renderContext)
{
  owner.lock()->notify(renderContext);
}

void Descriptor::getDescriptorValues(const RenderContext& renderContext, std::vector<DescriptorValue>& values) const
{
  for (auto& res : resources)
  {
    auto dsv = res->getDescriptorValue(renderContext);
    values.push_back(dsv);
  }
}

DescriptorSet::DescriptorSet(std::shared_ptr<DescriptorPool> p, std::shared_ptr<DescriptorSetLayout> l)
  : pool{ p }, layout{ l }
{
  poolIndex = pool->registerDescriptorSet(layout);
}

DescriptorSet::~DescriptorSet()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& desc : descriptors)
    desc.second->unregisterFromResources();
  descriptors.clear();

  for (auto& pdd : perObjectData)
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
      pool->deallocate(pdd.second.commonData, poolIndex, pdd.second.data[i].descriptorSet);
}

void DescriptorSet::validate( const RenderContext& renderContext )
{
  // validate descriptor pool and layout
  layout->validate(renderContext);

  // validate descriptors
  for (const auto& d : descriptors)
    d.second->validate(renderContext);

  // now check if descriptor set is dirty
  std::lock_guard<std::mutex> lock(mutex);
  if (renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKeyID(renderContext, pbPerSurface);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, DescriptorSetData(renderContext, swForEachImage) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  if (pddit->second.data[activeIndex].descriptorSet == VK_NULL_HANDLE)
  {
    pddit->second.data[activeIndex].descriptorSet = pool->allocate(renderContext, poolIndex);
    pddit->second.commonData                      = renderContext.device->getID();
  }

  std::map<uint32_t, std::vector<DescriptorValue>> values;
  uint32_t dsvSize = 0;
  for (const auto& d : descriptors)
  {
    std::vector<DescriptorValue> value;
    d.second->getDescriptorValues(renderContext, value);
    dsvSize += layout->getDescriptorBindingCount(d.first);
    values.insert({ d.first, value });
  }
  std::vector<VkWriteDescriptorSet>   writeDescriptorSets;
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
      writeDescriptorSet.dstSet          = pddit->second.data[activeIndex].descriptorSet;
      writeDescriptorSet.descriptorType  = layout->getDescriptorType(v.first);
      writeDescriptorSet.dstBinding      = v.first;
      writeDescriptorSet.descriptorCount = layout->getDescriptorBindingCount(v.first);
      switch (v.second[0].vType)
      {
      case DescriptorValue::Buffer:
        writeDescriptorSet.pBufferInfo = &bufferInfos[bufferInfosCurrentSize];
        for (const auto& dsv : v.second)
          bufferInfos[bufferInfosCurrentSize++] = dsv.bufferInfo;
        break;
      case DescriptorValue::Image:
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
  pddit->second.valid[activeIndex] = true;
  notifyCommandBuffers(activeIndex);
}

VkDescriptorSet DescriptorSet::getHandle(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto keyValue = getKeyID(renderContext, pbPerSurface);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    return VK_NULL_HANDLE;
  return pddit->second.data[renderContext.activeIndex].descriptorSet;
}

void DescriptorSet::invalidateOwners()
{
  for (auto& n : nodeOwners)
    n.lock()->invalidateDescriptorsAndParents();
}

void DescriptorSet::notify()
{
  for(auto& pdd : perObjectData)
    pdd.second.invalidate();
}

void DescriptorSet::notify(const RenderContext& renderContext)
{
  auto keyValue = getKeyID(renderContext, pbPerSurface);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, DescriptorSetData(renderContext,swForEachImage) }).first;
  pddit->second.invalidate();
}

void DescriptorSet::setDescriptor(uint32_t binding, const std::vector<std::shared_ptr<Resource>>& resources, VkDescriptorType descriptorType)
{
  CHECK_LOG_THROW(resources.empty(), "setDescriptor got empty vector of resources");
  CHECK_LOG_THROW(binding >= layout->getBindings().size(), "Binding out of bounds");
  CHECK_LOG_THROW(layout->getBindings().at(binding).descriptorType != descriptorType, "Binding " << binding << " with wrong descriptor type : " << descriptorType << " but should be " << layout->getBindings().at(binding).descriptorType);
  resetDescriptor(binding);
  std::lock_guard<std::mutex> lock(mutex);
  descriptors[binding] = std::make_shared<Descriptor>(std::dynamic_pointer_cast<DescriptorSet>(shared_from_this()), resources, descriptorType);
  descriptors[binding]->registerInResources();
  notify();
  invalidateOwners();
}

void DescriptorSet::setDescriptor(uint32_t binding, const std::vector<std::shared_ptr<Resource>>& resources)
{
  CHECK_LOG_THROW(resources.empty(), "setDescriptor got empty vector of resources");
  auto defaultType = resources[0]->getDefaultDescriptorType();
  CHECK_LOG_THROW(!defaultType.first, "Default descriptor type is not defined");
  setDescriptor(binding, resources, defaultType.second);
}

void DescriptorSet::setDescriptor(uint32_t binding, std::shared_ptr<Resource> resource, VkDescriptorType descriptorType)
{
  CHECK_LOG_THROW(binding >= layout->getBindings().size(), "Binding out of bounds");
  CHECK_LOG_THROW(layout->getBindings().at(binding).descriptorType != descriptorType, "Binding " << binding << " with wrong descriptor type : " << descriptorType << " but should be " << layout->getBindings().at(binding).descriptorType);
  resetDescriptor(binding);
  std::lock_guard<std::mutex> lock(mutex);
  descriptors[binding] = std::make_shared<Descriptor>(std::dynamic_pointer_cast<DescriptorSet>(shared_from_this()), resource, descriptorType);
  descriptors[binding]->registerInResources();
  notify();
  invalidateOwners();
}

void DescriptorSet::setDescriptor(uint32_t binding, std::shared_ptr<Resource> resource)
{
  auto defaultType = resource->getDefaultDescriptorType();
  CHECK_LOG_THROW(!defaultType.first, "Default descriptor type is not defined");
  setDescriptor(binding, resource, defaultType.second);
}

void DescriptorSet::resetDescriptor(uint32_t binding)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (descriptors.find(binding) != end(descriptors))
  {
    descriptors[binding]->unregisterFromResources();
    descriptors.erase(binding);
    notify();
    invalidateOwners();
  }
}

std::shared_ptr<Descriptor> DescriptorSet::getDescriptor(uint32_t binding)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = descriptors.find(binding);
  if (it == end(descriptors))
    return std::shared_ptr<Descriptor>();
  return it->second;
}

void DescriptorSet::addNode(std::shared_ptr<Node> node)
{
  nodeOwners.push_back(node);
}

void DescriptorSet::removeNode(std::shared_ptr<Node> node)
{
  auto it = std::find_if(begin(nodeOwners), end(nodeOwners), [node](std::weak_ptr<Node> p) -> bool { return p.lock() == node; });
  if (it != end(nodeOwners))
    nodeOwners.erase(it);
}
