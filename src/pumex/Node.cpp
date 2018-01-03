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
#include <pumex/Node.h>
#include <pumex/NodeVisitor.h>
#include <pumex/RenderContext.h>
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
  for (auto parent : parents)
    parent.lock()->accept(nv);
}

void Node::addParent(std::shared_ptr<Group> parent)
{
  parents.push_back(parent);
}

void Node::removeParent(std::shared_ptr<Group> parent)
{
  auto it = std::find_if(parents.begin(), parents.end(), [parent](std::weak_ptr<Group> p) -> bool { return p.lock() == parent; });
  if (it != parents.end())
    parents.erase(it);
}

void Node::setDescriptorSet(uint32_t index, std::shared_ptr<DescriptorSet> descriptorSet)
{
  descriptorSets[index] = descriptorSet;
  descriptorSet->addNode(std::dynamic_pointer_cast<Node>(shared_from_this()));
}

void Node::resetDescriptorSet(uint32_t index)
{
  auto it = descriptorSets.find(index);
  if (it == descriptorSets.end())
    return;
  it->second->removeNode(std::dynamic_pointer_cast<Node>(shared_from_this()));
  descriptorSets.erase(it);
}

void Node::validate(const RenderContext& renderContext)
{
//  LOG_ERROR << "Node::validate : " << getName() << std::endl;
  for (auto& descriptorSet : descriptorSets)
    descriptorSet.second->validate(renderContext);
  valid = true;
}

void Node::invalidate()
{
  if (valid)
  {
    valid = false;
    for (auto parent : parents)
      parent.lock()->invalidate();
    notifyCommandBuffers();
  }
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
  for (auto child : *this)
    child->accept(visitor);
}

void Group::addChild(std::shared_ptr<Node> child)
{
  children.push_back(child);
  child->addParent(std::dynamic_pointer_cast<Group>(shared_from_this()));
  invalidate();
}

bool Group::removeChild(std::shared_ptr<Node> child)
{
  auto it = std::find(children.begin(), children.end(), child);
  if (it == children.end())
    return false;
  child->removeParent(std::dynamic_pointer_cast<Group>(shared_from_this()));
  children.erase(it);
  invalidate();
  return true;
}

