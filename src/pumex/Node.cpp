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

void Node::setDescriptorSet(uint32_t index, std::shared_ptr<DescriptorSet> descriptorSet)
{
  std::lock_guard<std::mutex> lock(mutex);
  descriptorSets[index] = descriptorSet;
  descriptorSet->addNode(std::dynamic_pointer_cast<Node>(shared_from_this()));
  invalidate();
}

void Node::resetDescriptorSet(uint32_t index)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = descriptorSets.find(index);
  if (it == end(descriptorSets))
    return;
  it->second->removeNode(std::dynamic_pointer_cast<Node>(shared_from_this()));
  descriptorSets.erase(it);
}

bool Node::nodeValidate(const RenderContext& renderContext)
{
  for (auto& descriptorSet : descriptorSets)
    descriptorSet.second->validate(renderContext);

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
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return false;

  validate(renderContext);

  pddit->second.valid[activeIndex] = true;
  return true;
}

void Node::invalidate(const RenderContext& renderContext)
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
  if (pddit->second.anyValid())
  {
    pddit->second.invalidate();
    for (auto& parent : parents)
      parent.lock()->invalidate();
  }
}

void Node::invalidate(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(surface->getID());
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ surface->getID(), NodeData(surface->device.lock()->device, surface->surface, activeCount, swForEachImage) }).first;
  if (pddit->second.anyValid())
  {
    pddit->second.invalidate();
    for (auto& parent : parents)
      parent.lock()->invalidate();
  }
}

void Node::invalidate()
{
  bool anyValid = false;
  for (auto& pdd : perObjectData)
  {
    bool valid = pdd.second.anyValid();
    if (valid)
      pdd.second.invalidate();
    anyValid |= valid;
  }
  if (anyValid)
    for (auto& parent : parents)
      parent.lock()->invalidate();
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
  children.push_back(child);
  child->addParent(std::dynamic_pointer_cast<Group>(shared_from_this()));
  invalidate();
}

bool Group::removeChild(std::shared_ptr<Node> child)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = std::find(std::begin(children), std::end(children), child);
  if (it == std::end(children))
    return false;
  child->removeParent(std::dynamic_pointer_cast<Group>(shared_from_this()));
  children.erase(it);
  invalidate();
  return true;
}

void Group::validate(const RenderContext& renderContext)
{
}
