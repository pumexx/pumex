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
#include <pumex/RenderPass.h>
#include <pumex/Pipeline.h>
#include <pumex/AssetBufferNode.h>
#include <pumex/DispatchNode.h>
#include <pumex/DrawNode.h>

using namespace pumex;

RenderContextVisitor::RenderContextVisitor(TraversalMode tm, const RenderContext& rc)
  : NodeVisitor{ tm }, renderContext { rc }
{
}

FindSecondaryCommandBuffersVisitor::FindSecondaryCommandBuffersVisitor(const RenderContext& rc)
  : RenderContextVisitor{ AllChildren, rc }
{
}

void FindSecondaryCommandBuffersVisitor::apply(Node& node)
{
  if (node.hasSecondaryBuffer() && std::find(begin(nodes), end(nodes), &node)==end(nodes))
  {
    nodes.push_back(&node);
    if (renderContext.renderPass != nullptr)
      renderPasses.push_back(renderContext.renderPass->getHandle(renderContext));
    else
      renderPasses.push_back(VK_NULL_HANDLE);
    subPasses.push_back(renderContext.subpassIndex);
  }
  if (node.hasSecondaryBufferChildren())
    traverse(node);
}

ValidateNodeVisitor::ValidateNodeVisitor(const RenderContext& rc, bool bp)
  : RenderContextVisitor{ AllChildren , rc }, buildingPrimary{ bp }
{
}

void ValidateNodeVisitor::apply(Node& node)
{
  if (buildingPrimary && node.hasSecondaryBuffer())
    return;
  if (node.nodeValidate(renderContext))
  {
    traverse(node);
    node.setChildNodesValid(renderContext);
  }
}

ValidateDescriptorVisitor::ValidateDescriptorVisitor(const RenderContext& rc, bool bp)
  : RenderContextVisitor{ AllChildren, rc }, buildingPrimary{ bp }
{
}

void ValidateDescriptorVisitor::apply(Node& node)
{
  if (buildingPrimary && node.hasSecondaryBuffer())
    return;
  for (auto dit = node.descriptorSetBegin(); dit != node.descriptorSetEnd(); ++dit)
    dit->second->validate(renderContext);
  traverse(node);
}

CompleteRenderContextVisitor::CompleteRenderContextVisitor(RenderContext& rc)
  : NodeVisitor{ Parents }, renderContext{ rc }
{
  for (uint32_t i = 0; i < CRCV_TARGETS; ++i)
    targetCompleted[i] = false;
}

void CompleteRenderContextVisitor::apply(AssetBufferNode& node)
{
  if (!targetCompleted[0])
  {
    renderContext.setCurrentAssetBuffer(node.assetBuffer.get());
    renderContext.setCurrentRenderMask(node.renderMask);
    targetCompleted[0] = true;
  }
  bool allTargetsCompleted = true;
  for (uint32_t i = 0; i < CRCV_TARGETS; ++i)
    allTargetsCompleted = allTargetsCompleted && targetCompleted[i];
  if (!allTargetsCompleted)
    traverse(node);
}

BuildCommandBufferVisitor::BuildCommandBufferVisitor(const RenderContext& rc, CommandBuffer* cb, bool bp)
  : RenderContextVisitor{ AllChildren, rc }, commandBuffer{ cb }, buildingPrimary{ bp }
{
}

void BuildCommandBufferVisitor::apply(Node& node)
{
  if (buildingPrimary && node.hasSecondaryBuffer())
  {
    commandBuffer->executeCommandBuffer(renderContext, node.getSecondaryBuffer(renderContext).get());
    return;
  }
  applyDescriptorSets(node);
  traverse(node);
}

void BuildCommandBufferVisitor::apply(GraphicsPipeline& node)
{
  if (buildingPrimary && node.hasSecondaryBuffer())
  {
    commandBuffer->executeCommandBuffer(renderContext, node.getSecondaryBuffer(renderContext).get());
    return;
  }
  PipelineLayout* previousPL     = renderContext.setCurrentPipelineLayout(node.pipelineLayout.get());
  VkPipelineBindPoint previousBP = renderContext.setCurrentBindPoint(VK_PIPELINE_BIND_POINT_GRAPHICS);
  commandBuffer->cmdBindPipeline(renderContext, &node);
  applyDescriptorSets(node);
  traverse(node);
  // FIXME - bind previous ?
  renderContext.setCurrentPipelineLayout(previousPL);
  renderContext.setCurrentBindPoint(previousBP);
}

void BuildCommandBufferVisitor::apply(ComputePipeline& node)
{
  if (buildingPrimary && node.hasSecondaryBuffer())
  {
    commandBuffer->executeCommandBuffer(renderContext, node.getSecondaryBuffer(renderContext).get());
    return;
  }
  PipelineLayout* previousPL     = renderContext.setCurrentPipelineLayout(node.pipelineLayout.get());
  VkPipelineBindPoint previousBP = renderContext.setCurrentBindPoint(VK_PIPELINE_BIND_POINT_COMPUTE);
  commandBuffer->cmdBindPipeline(renderContext, &node);
  applyDescriptorSets(node);
  traverse(node);
  // FIXME - bind previous ?
  renderContext.setCurrentPipelineLayout(previousPL);
  renderContext.setCurrentBindPoint(previousBP);
}

void BuildCommandBufferVisitor::apply(AssetBufferNode& node)
{
  if (buildingPrimary && node.hasSecondaryBuffer())
  {
    commandBuffer->executeCommandBuffer(renderContext, node.getSecondaryBuffer(renderContext).get());
    return;
  }
  AssetBuffer* previousAB = renderContext.setCurrentAssetBuffer( node.assetBuffer.get() );
  uint32_t     previousRM = renderContext.setCurrentRenderMask( node.renderMask );
  applyDescriptorSets(node);
  commandBuffer->addSource(&node);
  node.assetBuffer->cmdBindVertexIndexBuffer(renderContext, commandBuffer, node.renderMask, node.vertexBinding);
  traverse(node);
  // FIXME - bind previous ?
  renderContext.setCurrentAssetBuffer(previousAB);
  renderContext.setCurrentRenderMask(previousRM);
}

void BuildCommandBufferVisitor::apply(DispatchNode& node)
{
  if (buildingPrimary && node.hasSecondaryBuffer())
  {
    commandBuffer->executeCommandBuffer(renderContext, node.getSecondaryBuffer(renderContext).get());
    return;
  }
  applyDescriptorSets(node);
  commandBuffer->cmdDispatch(node.getX(), node.getY(), node.getZ());
  traverse(node);
}

void BuildCommandBufferVisitor::apply(DrawNode& node)
{
  if (buildingPrimary && node.hasSecondaryBuffer())
  {
    commandBuffer->executeCommandBuffer(renderContext, node.getSecondaryBuffer(renderContext).get());
    return;
  }
  applyDescriptorSets(node);
  node.cmdDraw(renderContext, commandBuffer);
  traverse(node);
}

void BuildCommandBufferVisitor::applyDescriptorSets(Node& node)
{
  if (renderContext.currentPipelineLayout == nullptr)
    return;
  for (auto it = node.descriptorSetBegin(); it != node.descriptorSetEnd(); ++it )
  {
    commandBuffer->cmdBindDescriptorSets(renderContext, renderContext.currentPipelineLayout, it->first, it->second.get());
  }
}
