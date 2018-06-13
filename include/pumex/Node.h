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

  inline void                           setMask(uint32_t mask);
  inline uint32_t                       getMask() const;

  inline void                           setName(const std::string& name);
  inline const std::string&             getName() const;

  virtual void                          accept( NodeVisitor& visitor );

  virtual void                          traverse(NodeVisitor& visitor);
  virtual void                          ascend(NodeVisitor& nv);

  void                                  setDescriptorSet(uint32_t index, std::shared_ptr<DescriptorSet> descriptorSet);
  void                                  resetDescriptorSet(uint32_t index);
  std::shared_ptr<DescriptorSet>        getDescriptorSet(uint32_t index);

  // returns true if children need to be validated
  bool                                  nodeValidate(const RenderContext& renderContext);
  // marks children as validated
  void                                  setChildNodesValid(const RenderContext& renderContext);

  void                                  invalidateNodeAndParents();
  void                                  invalidateNodeAndParents(Surface* surface);
  void                                  invalidateDescriptorsAndParents();
  void                                  invalidateDescriptorsAndParents(Surface* surface);

  virtual void                          useSecondaryBuffer();
  inline bool                           hasSecondaryBuffer() const;
  std::shared_ptr<CommandBuffer>        getSecondaryBuffer(const RenderContext& renderContext);
  virtual bool                          hasSecondaryBufferChildren();

  virtual void                          validate(const RenderContext& renderContext) = 0;

  void                                  addParent(std::shared_ptr<Group> parent);
  void                                  removeParent(std::shared_ptr<Group> parent);
  bool                                  isInSecondaryBuffer();
protected:
  void                                  invalidateParentsNode();
  void                                  invalidateParentsNode(Surface* surface);
  void                                  invalidateParentsDescriptor();
  void                                  invalidateParentsDescriptor(Surface* surface);

  struct NodeInternal
  {
    NodeInternal()
      : childNodesValid{ false }, childDescriptorsValid{ false }, descriptorsValid{ false }
    {
    }
    bool                           childNodesValid;
    bool                           childDescriptorsValid;
    bool                           descriptorsValid;
  };
  struct NodeSecondaryCB
  {
    std::shared_ptr<CommandPool>   secondaryCommandPool; // secondary CB has its own pool because it generates CB in a separate thread/task
    std::shared_ptr<CommandBuffer> secondaryCommandBuffer;
  };

  typedef PerObjectData<NodeInternal, NodeSecondaryCB> NodeData;

  mutable std::mutex                                           mutex;
  uint32_t                                                     mask                   = 0xFFFFFFFF;
  std::vector<std::weak_ptr<Group>>                            parents;
  std::unordered_map<uint32_t, NodeData>                       perObjectData;
  std::string                                                  name;
  uint32_t                                                     activeCount            = 1;
  std::unordered_map<uint32_t, std::shared_ptr<DescriptorSet>> descriptorSets;
  bool                                                         secondaryBufferPresent = false;
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

  void                                   accept(NodeVisitor& visitor) override;
  void                                   traverse(NodeVisitor& visitor) override;

  virtual void                           addChild(std::shared_ptr<Node> child);
  virtual bool                           removeChild(std::shared_ptr<Node> child);

  void                                   useSecondaryBuffer() override;
  bool                                   hasSecondaryBufferChildren() override;
  void                                   checkChildrenForSecondaryBuffers();

  inline uint32_t                        getNumChildren();
  inline std::shared_ptr<Node>           getChild(uint32_t childIndex);

  void                                   validate(const RenderContext& renderContext) override;

protected:
  std::vector<std::shared_ptr<Node>>     children;
  bool                                   secondaryBufferChildren = false;

public:
  inline decltype(std::begin(children))  begin();
  inline decltype(std::end(children))    end();
  inline decltype(std::cbegin(children)) begin() const;
  inline decltype(std::cend(children))   end() const;

};

void                                   Node::setMask(uint32_t m)            { mask = m; }
uint32_t                               Node::getMask() const                { return mask; }
void                                   Node::setName(const std::string& n)  { name = n; }
const std::string&                     Node::getName() const                { return name; }
bool                                   Node::hasSecondaryBuffer() const     { return secondaryBufferPresent; }

uint32_t                               Group::getNumChildren()              { return children.size(); }
std::shared_ptr<Node>                  Group::getChild(uint32_t childIndex) { return children[childIndex]; }
decltype(std::begin(Group::children))  Group::begin()                       { return std::begin(children); }
decltype(std::end(Group::children))    Group::end()                         { return std::end(children); }
decltype(std::cbegin(Group::children)) Group::begin() const                 { return std::cbegin(children); }
decltype(std::cend(Group::children))   Group::end() const                   { return std::cend(children); }

}