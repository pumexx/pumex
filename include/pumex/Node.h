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
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <pumex/Export.h>
#include <pumex/Command.h>
#include <pumex/PerObjectData.h>

namespace pumex
{
	
class Group;
class NodeVisitor;
class DescriptorSet;
class RenderContext;

class PUMEX_EXPORT Node : public CommandBufferSource
{
public:
  Node();
  virtual ~Node();

  inline void               setMask(uint32_t mask);
  inline uint32_t           getMask() const;

  inline void               setName(const std::string& name);
  inline const std::string& getName() const;

  virtual void accept( NodeVisitor& visitor );

  virtual void traverse(NodeVisitor& visitor);
  virtual void ascend(NodeVisitor& nv);

  void setDescriptorSet(uint32_t index, std::shared_ptr<DescriptorSet> descriptorSet);
  void resetDescriptorSet(uint32_t index);

  bool nodeValidate(const RenderContext& renderContext);
  void invalidate(const RenderContext& renderContext);
  void invalidate(Surface* surface);
  void invalidate();

  virtual void validate(const RenderContext& renderContext) = 0;

  void addParent(std::shared_ptr<Group> parent);
  void removeParent(std::shared_ptr<Group> parent);
protected:
  typedef PerObjectData<uint32_t, uint32_t> NodeData; // actually node only stores information about node validity

  mutable std::mutex                                           mutex;
  uint32_t                                                     mask        = 0xFFFFFFFF;
  std::vector<std::weak_ptr<Group>>                            parents;
  std::unordered_map<uint32_t, std::shared_ptr<DescriptorSet>> descriptorSets;
  std::unordered_map<uint32_t, NodeData>                       perObjectData;
  std::string                                                  name;
  uint32_t                                                     activeCount = 1;

public:
  inline decltype(begin(descriptorSets))  descriptorSetBegin()       { return begin(descriptorSets); }
  inline decltype(end(descriptorSets))    descriptorSetEnd()         { return end(descriptorSets); }
  inline decltype(cbegin(descriptorSets)) descriptorSetBegin() const { return cbegin(descriptorSets); }
  inline decltype(cend(descriptorSets))   descriptorSetEnd() const   { return cend(descriptorSets); }
};


class PUMEX_EXPORT Group : public Node
{
public:
  Group();
  virtual ~Group();

  void accept(NodeVisitor& visitor) override;
  void traverse(NodeVisitor& visitor) override;

  virtual void                  addChild(std::shared_ptr<Node> child);
  virtual bool                  removeChild(std::shared_ptr<Node> child);

  inline uint32_t               getNumChildren();
  inline std::shared_ptr<Node>  getChild(uint32_t childIndex);

  void validate(const RenderContext& renderContext) override;

protected:
  std::vector<std::shared_ptr<Node>> children;

public:
  inline decltype(std::begin(children))  begin()       { return std::begin(children); }
  inline decltype(std::end(children))    end()         { return std::end(children); }
  inline decltype(std::cbegin(children)) begin() const { return std::cbegin(children); }
  inline decltype(std::cend(children))   end() const   { return std::cend(children); }

};

void                  Node::setMask(uint32_t m)            { mask = m; }
uint32_t              Node::getMask() const                { return mask; }
void                  Node::setName(const std::string& n)  { name = n; }
const std::string&    Node::getName() const                { return name; }

uint32_t              Group::getNumChildren()              { return children.size(); }
std::shared_ptr<Node> Group::getChild(uint32_t childIndex) { return children[childIndex]; }

}