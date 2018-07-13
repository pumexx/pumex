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

DescriptorPool::DescriptorPool(uint32_t dps)
  : defaultPoolSize(dps)
{
}

DescriptorPool::~DescriptorPool()
{
  for (auto& pddit : perObjectData)
    for(uint32_t i=0; i<pddit.second.data.size(); ++i)
      for( auto it : pddit.second.data[i].descriptorPools)
        vkDestroyDescriptorPool(pddit.second.device, std::get<1>(it.second), nullptr);
}

void DescriptorPool::registerPool(const RenderContext& renderContext, std::shared_ptr<DescriptorSetLayout> descriptorSetLayout)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto keyValue = getKeyID(renderContext, pbPerDevice);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, DescriptorPoolData(renderContext, swOnce) }).first;

  auto it = pddit->second.data[0].descriptorPools.find(descriptorSetLayout.get());
  CHECK_LOG_THROW(it != end(pddit->second.data[0].descriptorPools), "DescriptorPool::registerPool() : second attempt to register DescriptorSetLayout");

  std::vector<VkDescriptorPoolSize> poolSizes = descriptorSetLayout->getDescriptorPoolSize(descriptorSetLayout->getPreferredPoolSize());
  VkDescriptorPoolCreateInfo descriptorPoolCI{};
    descriptorPoolCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.poolSizeCount = poolSizes.size();
    descriptorPoolCI.pPoolSizes    = poolSizes.data();
    descriptorPoolCI.maxSets       = descriptorSetLayout->getPreferredPoolSize();
    descriptorPoolCI.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // we will free our descriptor sets manually
  VkDescriptorPool dPool;
  VK_CHECK_LOG_THROW(vkCreateDescriptorPool(pddit->second.device, &descriptorPoolCI, nullptr, &dPool), "Cannot create descriptor pool");
  pddit->second.data[0].descriptorPools.insert({ descriptorSetLayout.get(), std::make_tuple(descriptorSetLayout, dPool, descriptorSetLayout->getPreferredPoolSize(), 0) });
}

VkDescriptorPool DescriptorPool::addDescriptorSets(const RenderContext& renderContext, std::shared_ptr<DescriptorSetLayout> descriptorSetLayout, uint32_t numDescriptorSets)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto keyValue = getKeyID(renderContext, pbPerDevice);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, DescriptorPoolData(renderContext, swOnce) }).first;

  auto it = pddit->second.data[0].descriptorPools.find(descriptorSetLayout.get());
  CHECK_LOG_THROW(it == end(pddit->second.data[0].descriptorPools), "DescriptorPool::addDescriptorSets() : DescriptorSetLayout was not registered previously");

  // check if descriptor pool size was not excedeed
  std::weak_ptr<DescriptorSetLayout> descSetLayout;
  VkDescriptorPool pool;
  uint32_t maxSize, curSize;
  std::tie(descSetLayout, pool, maxSize, curSize) = it->second;
  CHECK_LOG_THROW(curSize + numDescriptorSets > maxSize, "DescriptorPool::addDescriptorSets() : too many descriptor sets allocated");
  return pool;
}

DescriptorSetLayout::DescriptorSetLayout(const std::vector<DescriptorSetLayoutBinding>& b)
  : bindings(b), preferredPoolSize{ 8 }
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

  // if descriptor set layout was not created, then pool for this descriptor set layout is empty/too small.
  renderContext.descriptorPool->registerPool(renderContext, shared_from_this());

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

VkDescriptorPool DescriptorSetLayout::addDescriptorSets(const RenderContext& renderContext, uint32_t numDescriptorSets)
{
  return renderContext.descriptorPool->addDescriptorSets(renderContext, shared_from_this(), numDescriptorSets);
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

DescriptorSet::DescriptorSet(std::shared_ptr<DescriptorSetLayout> l)
  : layout{ l }
{
}

DescriptorSet::~DescriptorSet()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& desc : descriptors)
    desc.second->unregisterFromResources();
  descriptors.clear();

  for (auto& pdd : perObjectData)
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
    {
      vkFreeDescriptorSets(pdd.second.device, pdd.second.data[i].pool, 1, &pdd.second.data[i].descriptorSet);
      // FIXME - desc sets are not released from DescriptoPool
    }
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
    pddit->second.data[activeIndex].pool = layout->addDescriptorSets(renderContext, 1);
    VkDescriptorSetLayout layoutHandle   = layout->getHandle(renderContext);

    VkDescriptorSetAllocateInfo descriptorSetAinfo{};
      descriptorSetAinfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      descriptorSetAinfo.descriptorPool     = pddit->second.data[activeIndex].pool;
      descriptorSetAinfo.descriptorSetCount = 1;
      descriptorSetAinfo.pSetLayouts        = &layoutHandle;
    VK_CHECK_LOG_THROW(vkAllocateDescriptorSets(pddit->second.device, &descriptorSetAinfo, &pddit->second.data[activeIndex].descriptorSet), "Cannot allocate descriptor sets");
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
