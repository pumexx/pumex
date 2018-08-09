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
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/NodeVisitor.h>
#include <pumex/RenderContext.h>

namespace pumex
{

class CommandBuffer;

// NodeVisitor subclass that provides RenderContext for different shenanigans
class PUMEX_EXPORT RenderContextVisitor : public NodeVisitor
{
public:
  RenderContextVisitor(TraversalMode traversalMode, const RenderContext& renderContext);

  RenderContext renderContext;
};

// visitor responsible for identyfying secondary buffers in a tree ( dag )
class PUMEX_EXPORT FindSecondaryCommandBuffersVisitor : public RenderContextVisitor
{
public:
  FindSecondaryCommandBuffersVisitor(const RenderContext& renderContext);

  void apply(Node& node) override;

  std::vector<Node*>        nodes;
  std::vector<VkRenderPass> renderPasses;
  std::vector<uint32_t>     subPasses;
};

// Visitor that validates all dirty nodes ( pipelines etc ).
// Validation means sending all data ( buffers, images ) to GPU before building command buffers
class PUMEX_EXPORT ValidateNodeVisitor : public RenderContextVisitor
{
public:
  ValidateNodeVisitor(const RenderContext& renderContext, bool buildingPrimary);

  void apply(Node& node) override;

  bool buildingPrimary;
};

// Visitor that validates all dirty descriptor sets and descriptors ( updates them before building command buffers )
class PUMEX_EXPORT ValidateDescriptorVisitor : public RenderContextVisitor
{
public:
  ValidateDescriptorVisitor(const RenderContext& renderContext, bool buildingPrimary);

  void apply(Node& node) override;

  bool buildingPrimary;
};

// Visitor that collects missing data for render contexts while building secondary command buffers
const uint32_t CRCV_TARGETS = 1;

class PUMEX_EXPORT CompleteRenderContextVisitor : public NodeVisitor
{
public:
  CompleteRenderContextVisitor(RenderContext& renderContext);

  void apply(AssetBufferNode& node) override;
protected:
  RenderContext& renderContext;
  bool           targetCompleted[CRCV_TARGETS];
};

// Visitor that builds command buffers
class PUMEX_EXPORT BuildCommandBufferVisitor : public RenderContextVisitor
{
public:
  BuildCommandBufferVisitor(const RenderContext& renderContext, CommandBuffer* commandBuffer, bool buildingPrimary);

  void apply(Node& node) override;
  void apply(GraphicsPipeline& node) override;
  void apply(ComputePipeline& node) override;
  void apply(AssetBufferNode& node) override;
  void apply(DispatchNode& node) override;
  void apply(DrawNode& node) override;

  void applyDescriptorSets(Node& node);

  // elements of the context that are constant through visitor work
  CommandBuffer* commandBuffer;
  bool           buildingPrimary;
};

}
