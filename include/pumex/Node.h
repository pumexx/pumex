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
#pragma once
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

class Device;
class Surface;

class Group;
class NodeVisitor;

class PUMEX_EXPORT Node : public std::enable_shared_from_this<Node>
{
public:
  Node();
  virtual ~Node();

  inline void     setMask(uint32_t mask);
  inline uint32_t getMask();

  virtual void accept( NodeVisitor& visitor );

  virtual void traverse(NodeVisitor& visitor);
  virtual void ascend(NodeVisitor& nv);

  void addParent(std::shared_ptr<Group> parent);
  void removeParent(std::shared_ptr<Group> parent);

  void dirtyBound();
protected:
  uint32_t                          mask = 0xFFFFFFFF;
  std::vector<std::weak_ptr<Group>> parents;
  bool                              boundDirty = true;
};

class PUMEX_EXPORT ComputeNode : public Node
{
public:
  ComputeNode();
  virtual ~ComputeNode();

};

class PUMEX_EXPORT Group : public Node
{
protected:
  std::vector<std::shared_ptr<Node>> children;
public:
  Group();
  virtual ~Group();

  void traverse(NodeVisitor& visitor) override;


  virtual void addChild(std::shared_ptr<Node> child);
  virtual bool removeChild(std::shared_ptr<Node> child);

  inline decltype(children.begin())  childrenBegin()       { return children.begin(); }
  inline decltype(children.end())    childrenEnd()         { return children.end(); }
  inline decltype(children.cbegin()) childrenBegin() const { return children.cbegin(); }
  inline decltype(children.cend())   childrenEnd() const   { return children.cend(); }

};

class PUMEX_EXPORT NodeVisitor
{
public:
  enum TraversalMode { None, Parents, AllChildren, ActiveChildren };

  NodeVisitor(TraversalMode traversalMode = None);

  inline void     setMask(uint32_t mask);
  inline uint32_t getMask();

  inline void push(Node* node);
  inline void pop();

  void traverse(Node& node);

  virtual void apply(Node& node);
  virtual void apply(ComputeNode& node);
  virtual void apply(Group& node);


protected:
  uint32_t           mask = 0xFFFFFFFF;
  TraversalMode      traversalMode;
  std::vector<Node*> nodePath;
};

class PUMEX_EXPORT GPUUpdateVisitor : public NodeVisitor
{
public:
  GPUUpdateVisitor(Surface* surface);

  Surface* surface;
  Device*  device;
  VkDevice vkDevice;
  uint32_t imageIndex;
  uint32_t imageCount;
};

void     Node::setMask(uint32_t m)        { mask = m; }
uint32_t Node::getMask()                  { return mask; }
void     NodeVisitor::setMask(uint32_t m) { mask = m; }
uint32_t NodeVisitor::getMask()           { return mask; }
void     NodeVisitor::push(Node* node)    { nodePath.push_back(node); }
void     NodeVisitor::pop()               { nodePath.pop_back(); }


}