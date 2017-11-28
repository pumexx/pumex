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

class NodeGroup;
class NodeVisitor;

class PUMEX_EXPORT Node
{
public:
  Node();
  virtual ~Node();

  virtual void apply( NodeVisitor& visitor ) = 0;
protected:
  std::vector<std::weak_ptr<NodeGroup>> parents;
};

class PUMEX_EXPORT ComputeNode : public Node
{
public:
  ComputeNode();
  virtual ~ComputeNode();

};

class PUMEX_EXPORT NodeGroup : public Node
{
public:
  NodeGroup();
  virtual ~NodeGroup();

  void addChild(std::shared_ptr<Node> child);

protected:
  std::vector<std::shared_ptr<Node>> children;
};

class PUMEX_EXPORT NodeVisitor
{
public:
  NodeVisitor();
};

class PUMEX_EXPORT UpdateVisitor : public NodeVisitor
{
public:
  UpdateVisitor(Surface* surface);

  Surface* surface;
  Device*  device;
  VkDevice vkDevice;
  uint32_t imageIndex;
  uint32_t imageCount;

};


}