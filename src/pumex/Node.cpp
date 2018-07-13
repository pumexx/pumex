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

#include <pumex/Node.h>
#include <pumex/NodeVisitor.h>
#include <pumex/Descriptor.h>
#include <pumex/RenderContext.h>
#include <pumex/Surface.h>
#include <pumex/utils/Log.h>
#include <algorithm>

using namespace pumex;

Node::Node()
{
}

Node::~Node()
{
  parents.clear();
}

std::weak_ptr<Group> Node::getParent(uint32_t index)
{
  return parents[index];
}

void Node::accept(NodeVisitor& visitor)
{
  if (visitor.getMask() && mask)
  {
    visitor.push(this);
    visitor.apply(*this);
    visitor.pop();
  }
}

void Node::traverse(NodeVisitor& visitor)
{
  // simple node does not traverse anywhere
}

void Node::ascend(NodeVisitor& nv)
{
  for (auto& parent : parents)
    parent.lock()->accept(nv);
}

void Node::addParent(std::shared_ptr<Group> parent)
{
  parents.push_back(parent);
}

void Node::removeParent(std::shared_ptr<Group> parent)
{
  auto it = std::find_if(begin(parents), end(parents), [parent](std::weak_ptr<Group> p) -> bool { return p.lock() == parent; });
  if (it != end(parents))
    parents.erase(it);
}

bool Node::isInSecondaryBuffer()
{
  if (secondaryBufferPresent)
    return true;
  for (auto& p : parents)
    if (p.lock()->isInSecondaryBuffer())
      return true;
  return false;
}

void Node::setDescriptorSet(uint32_t index, std::shared_ptr<DescriptorSet> descriptorSet)
{
  std::lock_guard<std::mutex> lock(mutex);
  descriptorSets[index] = descriptorSet;
  descriptorSet->addNode(std::dynamic_pointer_cast<Node>(shared_from_this()));
  invalidateParentsDescriptor();
}

void Node::resetDescriptorSet(uint32_t index)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = descriptorSets.find(index);
  if (it == end(descriptorSets))
    return;
  it->second->removeNode(std::dynamic_pointer_cast<Node>(shared_from_this()));
  descriptorSets.erase(it);
  invalidateParentsDescriptor();
}

std::shared_ptr<DescriptorSet> Node::getDescriptorSet(uint32_t index)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = descriptorSets.find(index);
  if (it == end(descriptorSets))
    return std::shared_ptr<DescriptorSet>();
  return it->second;
}

bool Node::nodeValidate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (activeCount < renderContext.imageCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKeyID(renderContext, pbPerSurface);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, NodeData(renderContext, swForEachImage) }).first;
  if (secondaryBufferPresent && pddit->second.commonData.secondaryCommandPool==nullptr)
  {
    pddit->second.commonData.secondaryCommandPool   = std::make_shared<CommandPool>(renderContext.surface->getPresentationQueue()->familyIndex);
    pddit->second.commonData.secondaryCommandPool->validate(renderContext.device);
    pddit->second.commonData.secondaryCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_SECONDARY, renderContext.device, pddit->second.commonData.secondaryCommandPool, activeCount);
  }
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return !pddit->second.data[activeIndex].childNodesValid;

  validate(renderContext);

  pddit->second.valid[activeIndex] = true;
  return !pddit->second.data[activeIndex].childNodesValid;
}

void Node::setChildNodesValid(const RenderContext& renderContext)
{
  auto keyValue = getKeyID(renderContext, pbPerSurface);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    return;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  pddit->second.data[activeIndex].childNodesValid = true;
}

void Node::invalidateNodeAndParents()
{
  for (auto& pdd : perObjectData)
    pdd.second.invalidate();
  if(!hasSecondaryBuffer())
    for (auto& parent : parents)
      parent.lock()->invalidateParentsNode();
}

void Node::invalidateNodeAndParents(Surface* surface)
{
  if (activeCount < surface->getImageCount())
  {
    activeCount = surface->getImageCount();
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto pddit = perObjectData.find(surface->getID());
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ surface->getID(), NodeData(surface->device.lock()->device, surface->surface, activeCount, swForEachImage) }).first;
  if (secondaryBufferPresent && pddit->second.commonData.secondaryCommandPool==nullptr)
  {
    Device* device = surface->device.lock().get();
    pddit->second.commonData.secondaryCommandPool = std::make_shared<CommandPool>(surface->getPresentationQueue()->familyIndex);
    pddit->second.commonData.secondaryCommandPool->validate(device);
    pddit->second.commonData.secondaryCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_SECONDARY, device, pddit->second.commonData.secondaryCommandPool, activeCount);
  }
  pddit->second.invalidate();
  if (!hasSecondaryBuffer())
    for (auto& parent : parents)
      parent.lock()->invalidateParentsNode(surface);
}

void Node::invalidateDescriptorsAndParents()
{
  for (auto& pdd : perObjectData)
    for (uint32_t i = 0; i < activeCount; ++i)
      pdd.second.data[i].descriptorsValid = false;
  if (!hasSecondaryBuffer())
    for (auto& parent : parents)
      parent.lock()->invalidateParentsDescriptor();
}

void Node::invalidateDescriptorsAndParents(Surface* surface)
{
  if (activeCount < surface->getImageCount())
  {
    activeCount = surface->getImageCount();
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto pddit = perObjectData.find(surface->getID());
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ surface->getID(), NodeData(surface->device.lock()->device, surface->surface, activeCount, swForEachImage) }).first;
  if (secondaryBufferPresent && pddit->second.commonData.secondaryCommandPool == nullptr)
  {
    Device* device = surface->device.lock().get();
    pddit->second.commonData.secondaryCommandPool = std::make_shared<CommandPool>(surface->getPresentationQueue()->familyIndex);
    pddit->second.commonData.secondaryCommandPool->validate(device);
    pddit->second.commonData.secondaryCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_SECONDARY, device, pddit->second.commonData.secondaryCommandPool, activeCount);
  }
  for (uint32_t i = 0; i < activeCount; ++i)
    pddit->second.data[i].descriptorsValid = false;
  if (!hasSecondaryBuffer())
    for (auto& parent : parents)
      parent.lock()->invalidateParentsDescriptor(surface);
}

void Node::useSecondaryBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  CHECK_LOG_THROW(isInSecondaryBuffer() && !secondaryBufferPresent, "Cannot set secondary buffer : one of the parents uses secondary buffer already");
  secondaryBufferPresent = true;
  invalidateNodeAndParents();
  for (auto& p : parents)
    p.lock()->checkChildrenForSecondaryBuffers();
}

std::shared_ptr<CommandBuffer> Node::getSecondaryBuffer(const RenderContext& renderContext)
{ 
  std::lock_guard<std::mutex> lock(mutex);
  if (activeCount < renderContext.imageCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKeyID(renderContext, pbPerSurface);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, NodeData(renderContext, swForEachImage) }).first;
  if (secondaryBufferPresent && pddit->second.commonData.secondaryCommandPool == nullptr)
  {
    pddit->second.commonData.secondaryCommandPool   = std::make_shared<CommandPool>(renderContext.surface->getPresentationQueue()->familyIndex);
    pddit->second.commonData.secondaryCommandPool->validate(renderContext.device);
    pddit->second.commonData.secondaryCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_SECONDARY, renderContext.device, pddit->second.commonData.secondaryCommandPool, activeCount);
  }
  return pddit->second.commonData.secondaryCommandBuffer;
}

std::shared_ptr<CommandPool> Node::getSecondaryCommandPool(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (activeCount < renderContext.imageCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKeyID(renderContext, pbPerSurface);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, NodeData(renderContext, swForEachImage) }).first;
  if (secondaryBufferPresent && pddit->second.commonData.secondaryCommandPool == nullptr)
  {
    pddit->second.commonData.secondaryCommandPool = std::make_shared<CommandPool>(renderContext.surface->getPresentationQueue()->familyIndex);
    pddit->second.commonData.secondaryCommandPool->validate(renderContext.device);
    pddit->second.commonData.secondaryCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_SECONDARY, renderContext.device, pddit->second.commonData.secondaryCommandPool, activeCount);
  }
  return pddit->second.commonData.secondaryCommandPool;
}

bool Node::hasSecondaryBufferChildren()
{
  return false; // only groups can have children
}

void Node::invalidateParentsNode()
{
  bool needInvalidateParents = false;
  for (auto& pdd : perObjectData)
  {
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
    {
      if (pdd.second.data[i].childNodesValid)
      {
        pdd.second.data[i].childNodesValid = false;
        needInvalidateParents = true;
      }
    }
  }
  if( needInvalidateParents &&  !hasSecondaryBuffer() )
    for (auto& parent : parents)
      parent.lock()->invalidateParentsNode();
}

void Node::invalidateParentsNode(Surface* surface)
{
  if (activeCount < surface->getImageCount())
  {
    activeCount = surface->getImageCount();
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto pddit = perObjectData.find(surface->getID());
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ surface->getID(), NodeData(surface->device.lock()->device, surface->surface, activeCount, swForEachImage) }).first;
  if (secondaryBufferPresent && pddit->second.commonData.secondaryCommandPool == nullptr)
  {
    pddit->second.commonData.secondaryCommandPool   = std::make_shared<CommandPool>(surface->getPresentationQueue()->familyIndex);
    Device* device = surface->device.lock().get();
    pddit->second.commonData.secondaryCommandPool->validate(device);
    pddit->second.commonData.secondaryCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_SECONDARY, device, pddit->second.commonData.secondaryCommandPool, activeCount);
  }

  bool needInvalidateParents = false;
  for (uint32_t i = 0; i < pddit->second.data.size(); ++i)
  {
    if (pddit->second.data[i].childNodesValid)
    {
      pddit->second.data[i].childNodesValid = false;
      needInvalidateParents = true;
    }
  }
  if (needInvalidateParents && !hasSecondaryBuffer())
    for (auto& parent : parents)
      parent.lock()->invalidateParentsNode(surface);
}

void Node::invalidateParentsDescriptor()
{
  bool needInvalidateParents = false;
  for (auto& pdd : perObjectData)
  {
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
    {
      if (pdd.second.data[i].descriptorsValid)
      {
        pdd.second.data[i].descriptorsValid = false;
        needInvalidateParents = true;
      }
    }
  }
  if (needInvalidateParents && !hasSecondaryBuffer())
    for (auto& parent : parents)
      parent.lock()->invalidateParentsDescriptor();
}

void Node::invalidateParentsDescriptor(Surface* surface)
{
  if (activeCount < surface->getImageCount())
  {
    activeCount = surface->getImageCount();
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto pddit = perObjectData.find(surface->getID());
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ surface->getID(), NodeData(surface->device.lock()->device, surface->surface, activeCount, swForEachImage) }).first;
  if (secondaryBufferPresent && pddit->second.commonData.secondaryCommandPool == nullptr)
  {
    Device* device = surface->device.lock().get();
    pddit->second.commonData.secondaryCommandPool   = std::make_shared<CommandPool>(surface->getPresentationQueue()->familyIndex);
    pddit->second.commonData.secondaryCommandPool->validate(device);
    pddit->second.commonData.secondaryCommandBuffer = std::make_shared<CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_SECONDARY, device, pddit->second.commonData.secondaryCommandPool, activeCount);
  }

  bool needInvalidateParents = false;
  for (uint32_t i = 0; i < pddit->second.data.size(); ++i)
  {
    if (pddit->second.data[i].descriptorsValid)
    {
      pddit->second.data[i].descriptorsValid = false;
      needInvalidateParents = true;
    }
  }
  if (needInvalidateParents && !hasSecondaryBuffer())
    for (auto& parent : parents)
      parent.lock()->invalidateParentsDescriptor(surface);
}

Group::Group()
{
}

Group::~Group()
{
  children.clear();
}

void Group::accept(NodeVisitor& visitor)
{
  if (visitor.getMask() && mask)
  {
    visitor.push(this);
    visitor.apply(*this);
    visitor.pop();
  }
}

void Group::traverse(NodeVisitor& visitor)
{
  for (auto& child : *this)
    child->accept(visitor);
}

void Group::addChild(std::shared_ptr<Node> child)
{
  std::lock_guard<std::mutex> lock(mutex);
  CHECK_LOG_THROW(isInSecondaryBuffer() && ( child->hasSecondaryBuffer() || child->hasSecondaryBufferChildren() ), "Cannot add child : both parent and child have secondary buffers already")
  children.push_back(child);
  child->addParent(std::dynamic_pointer_cast<Group>(shared_from_this()));
  checkChildrenForSecondaryBuffers();
  child->invalidateNodeAndParents();
}

bool Group::removeChild(std::shared_ptr<Node> child)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = std::find(std::begin(children), std::end(children), child);
  if (it == std::end(children))
    return false;
  child->removeParent(std::dynamic_pointer_cast<Group>(shared_from_this()));
  children.erase(it);
  checkChildrenForSecondaryBuffers();
  invalidateParentsNode();
  child->invalidateNodeAndParents();
  return true;
}

void Group::useSecondaryBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  CHECK_LOG_THROW(hasSecondaryBufferChildren(), "Cannot set secondary buffer : one of the children uses secondary buffer already");
  CHECK_LOG_THROW(isInSecondaryBuffer() && !secondaryBufferPresent, "Cannot set secondary buffer : one of the parents uses secondary buffer already");
  secondaryBufferPresent = true;
  invalidateNodeAndParents();
  for (auto& p : parents)
    p.lock()->checkChildrenForSecondaryBuffers();
}

bool Group::hasSecondaryBufferChildren()
{
  return secondaryBufferChildren;
}

void Group::checkChildrenForSecondaryBuffers()
{
  bool value = false;
  for (auto it = std::begin(children); it != std::end(children); ++it)
    value = value || (*it)->hasSecondaryBuffer() || (*it)->hasSecondaryBufferChildren();
  secondaryBufferChildren = value;
  for (auto& parent : parents)
    parent.lock()->checkChildrenForSecondaryBuffers();
}

void Group::validate(const RenderContext& renderContext)
{
}

