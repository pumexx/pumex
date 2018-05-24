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
#include <pumex/RenderVisitors.h>
#include <pumex/Descriptor.h>
#include <pumex/Pipeline.h>
#include <pumex/AssetBufferNode.h>
#include <pumex/AssetNode.h>
#include <pumex/DispatchNode.h>
#include <pumex/Text.h>

using namespace pumex;

ValidateGPUVisitor::ValidateGPUVisitor(const RenderContext& rc, bool vrg)
  : NodeVisitor{ AllChildren }, renderContext{ rc }, validateRenderGraphs{ vrg }
{
}

void ValidateGPUVisitor::apply(Node& node)
{
  if (node.nodeValidate(renderContext))
  {
    traverse(node);
    node.setChildrenValid(renderContext);
  }
}

BuildCommandBufferVisitor::BuildCommandBufferVisitor(const RenderContext& rc, CommandBuffer* cb)
  : NodeVisitor{ AllChildren }, renderContext{ rc }, commandBuffer{ cb }
{

}

void BuildCommandBufferVisitor::apply(Node& node)
{
  applyDescriptorSets(node);
  traverse(node);
}

void BuildCommandBufferVisitor::apply(GraphicsPipeline& node)
{
  Pipeline* previous = renderContext.setCurrentPipeline(&node);
  commandBuffer->cmdBindPipeline(renderContext, &node);
  applyDescriptorSets(node);
  traverse(node);
  // FIXME - bind previous ?
  renderContext.setCurrentPipeline(previous);
}

void BuildCommandBufferVisitor::apply(ComputePipeline& node)
{
  Pipeline* previous = renderContext.setCurrentPipeline(&node);
  commandBuffer->cmdBindPipeline(renderContext, &node);
  applyDescriptorSets(node);
  traverse(node);
  // FIXME - bind previous ?
  renderContext.setCurrentPipeline(previous);
}

void BuildCommandBufferVisitor::apply(AssetBufferNode& node)
{
  AssetBufferNode* previous = renderContext.setCurrentAssetBufferNode( &node );
  applyDescriptorSets(node);
  commandBuffer->addSource(&node);
  node.assetBuffer->cmdBindVertexIndexBuffer(renderContext, commandBuffer, node.renderMask, node.vertexBinding);
  traverse(node);
  // FIXME - bind previous ?
  renderContext.setCurrentAssetBufferNode(previous);
}

void BuildCommandBufferVisitor::apply(AssetBufferDrawObject& node)
{
  if (renderContext.currentAssetBufferNode == nullptr)
    return;
  applyDescriptorSets(node);
  renderContext.currentAssetBufferNode->assetBuffer->cmdDrawObject(renderContext, commandBuffer, renderContext.currentAssetBufferNode->renderMask, node.typeID, node.firstInstance, node.getDistanceToViewer());
  traverse(node);
}

void BuildCommandBufferVisitor::apply(AssetBufferIndirectDrawObjects& node)
{
  if (renderContext.currentAssetBufferNode == nullptr)
    return;
  applyDescriptorSets(node);
  renderContext.currentAssetBufferNode->assetBuffer->cmdDrawObjectsIndirect(renderContext, commandBuffer, node.getDrawCommands());
  traverse(node);
}

void BuildCommandBufferVisitor::apply(AssetNode& node)
{
  applyDescriptorSets(node);
  node.cmdDraw(renderContext, commandBuffer);
  traverse(node);
}

void BuildCommandBufferVisitor::apply(DispatchNode& node)
{
  applyDescriptorSets(node);
  commandBuffer->cmdDispatch(node.getX(), node.getY(), node.getZ());
  traverse(node);
}

void BuildCommandBufferVisitor::apply(Text& node)
{
  applyDescriptorSets(node);
  node.cmdDraw(renderContext, commandBuffer);
  traverse(node);
}

void BuildCommandBufferVisitor::applyDescriptorSets(Node& node)
{
  // FIXME : for now descriptor sets must be set below pipeline in a scene graph
  // It would be better if descriptor sets are collected and set in a moment of draw.
  if (renderContext.currentPipeline == nullptr)
    return;
  for (auto it = node.descriptorSetBegin(); it != node.descriptorSetEnd(); ++it )
  {
    commandBuffer->cmdBindDescriptorSets(renderContext, renderContext.currentPipeline->pipelineLayout.get(), it->first, it->second.get());
  }
}
