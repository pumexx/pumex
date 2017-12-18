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
#include <unordered_map>
#include <mutex>
#include <pumex/Export.h>
#include <pumex/Command.h>

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

  inline void        setMask(uint32_t mask);
  inline uint32_t    getMask();

  inline void        setName(const std::string& name);
  inline std::string getName();

  virtual void accept( NodeVisitor& visitor );

  virtual void traverse(NodeVisitor& visitor);
  virtual void ascend(NodeVisitor& nv);

  void addParent(std::shared_ptr<Group> parent);
  void removeParent(std::shared_ptr<Group> parent);

  void setDescriptorSet(uint32_t index, std::shared_ptr<DescriptorSet> descriptorSet);
  void resetDescriptorSet(uint32_t index);

  virtual void validate(const RenderContext& renderContext);
  void invalidate();

protected:
  mutable std::mutex                                           mutex;
  uint32_t                                                     mask = 0xFFFFFFFF;
  std::vector<std::weak_ptr<Group>>                            parents;
  std::unordered_map<uint32_t, std::shared_ptr<DescriptorSet>> descriptorSets;
  std::string                                                  name;
  bool                                                         valid = false;
};


class PUMEX_EXPORT Group : public Node
{
protected:
  std::vector<std::shared_ptr<Node>> children;
public:
  Group();
  virtual ~Group();

  void traverse(NodeVisitor& visitor) override;

  virtual void                  addChild(std::shared_ptr<Node> child);
  virtual bool                  removeChild(std::shared_ptr<Node> child);

  inline uint32_t               getNumChildren();
  inline std::shared_ptr<Node>  getChild(uint32_t childIndex);

  inline decltype(children.begin())  begin()       { return children.begin(); }
  inline decltype(children.end())    end()         { return children.end(); }
  inline decltype(children.cbegin()) begin() const { return children.cbegin(); }
  inline decltype(children.cend())   end() const   { return children.cend(); }

};

void                  Node::setMask(uint32_t m)            { mask = m; }
uint32_t              Node::getMask()                      { return mask; }
void                  Node::setName(const std::string& n)  { name = n; }
std::string           Node::getName()                      { return name; }

uint32_t              Group::getNumChildren()              { return children.size(); }
std::shared_ptr<Node> Group::getChild(uint32_t childIndex) { return children[childIndex]; }

}