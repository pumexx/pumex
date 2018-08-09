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

#pragma once
#include <pumex/Export.h>
#include <pumex/Node.h>

namespace pumex
{

class GraphicsPipeline;
class ComputePipeline;
class AssetBufferNode;
class DispatchNode;
class DrawNode;

// Node visitor is a class allowing user to visit direct acyclic graphs
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
  virtual void apply(Group& node);
  virtual void apply(GraphicsPipeline& node);
  virtual void apply(ComputePipeline& node);
  virtual void apply(AssetBufferNode& node);
  virtual void apply(DispatchNode& node);
  virtual void apply(DrawNode& node);

protected:
  uint32_t           mask = 0xFFFFFFFF;
  TraversalMode      traversalMode;
  std::vector<Node*> nodePath;
};

void                  NodeVisitor::setMask(uint32_t m)     { mask = m; }
uint32_t              NodeVisitor::getMask()               { return mask; }
void                  NodeVisitor::push(Node* node)        { nodePath.push_back(node); }
void                  NodeVisitor::pop()                   { nodePath.pop_back(); }

}
