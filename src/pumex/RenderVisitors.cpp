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
#include <pumex/RenderVisitors.h>
#include <pumex/RenderWorkflow.h>
#include <pumex/AssetBufferNode.h>
#include <pumex/Text.h>

using namespace pumex;

ValidateGPUVisitor::ValidateGPUVisitor(Surface* s)
  : NodeVisitor{ AllChildren }, renderContext(s)
{
}

void ValidateGPUVisitor::apply(Node& node)
{
  LOG_ERROR << "ValidateGPUVisitor::apply(Node& node) : " << node.getName() << std::endl;
  node.validate(renderContext);
  traverse(node);
}

void ValidateGPUVisitor::apply(GraphicsPipeline& node)
{
  LOG_ERROR << "ValidateGPUVisitor::apply(GraphicsPipeline& node) : " << node.getName() << std::endl;
  node.validate(renderContext);
  traverse(node);
}

void ValidateGPUVisitor::apply(ComputePipeline& node)
{
  LOG_ERROR << "ValidateGPUVisitor::apply(ComputePipeline& node) : " << node.getName() << std::endl;
  node.validate(renderContext);
  traverse(node);
}

void ValidateGPUVisitor::apply(AssetBufferNode& node)
{
  LOG_ERROR << "ValidateGPUVisitor::apply(AssetBufferNode& node) : " << node.getName() << std::endl;
  node.validate(renderContext);
  traverse(node);
}

void ValidateGPUVisitor::apply(AssetBufferDrawObject& node)
{
  LOG_ERROR << "ValidateGPUVisitor::apply(AssetBufferDrawObject& node) : " << node.getName() << std::endl;
  node.validate(renderContext);
  traverse(node);
}

void ValidateGPUVisitor::apply(Text& node)
{
  LOG_ERROR << "ValidateGPUVisitor::apply(Text& node) : " << node.getName() << std::endl;
  node.validate(renderContext);
  traverse(node);
}


BuildCommandBufferVisitor::BuildCommandBufferVisitor(Surface* s, CommandBuffer* cb)
  : NodeVisitor{ AllChildren }, renderContext(s), commandBuffer{ cb }
{

}

void BuildCommandBufferVisitor::apply(Node& node)
{
  LOG_ERROR << "BuildCommandBufferVisitor::apply(Node& node) : " << node.getName() << std::endl;
  applyDescriptorSets(node);
  traverse(node);
}

void BuildCommandBufferVisitor::apply(GraphicsPipeline& node)
{
  LOG_ERROR << "BuildCommandBufferVisitor::apply(GraphicsPipeline& node) : " << node.getName() << std::endl;
  Pipeline* previous = renderContext.setCurrentPipeline(&node);
  commandBuffer->cmdBindPipeline(&node);
  applyDescriptorSets(node);
  traverse(node);
  // FIXME - bind previous ?
  renderContext.setCurrentPipeline(previous);
}

void BuildCommandBufferVisitor::apply(ComputePipeline& node)
{
  LOG_ERROR << "BuildCommandBufferVisitor::apply(ComputePipeline& node) : " << node.getName() << std::endl;
  Pipeline* previous = renderContext.setCurrentPipeline(&node);
  commandBuffer->cmdBindPipeline(&node);
  applyDescriptorSets(node);
  traverse(node);
  // FIXME - bind previous ?
  renderContext.setCurrentPipeline(previous);
}

void BuildCommandBufferVisitor::apply(AssetBufferNode& node)
{
  LOG_ERROR << "BuildCommandBufferVisitor::apply(AssetBufferNode& node) : " << node.getName() << std::endl;
  AssetBufferNode* previous = renderContext.setCurrentAssetBufferNode( &node );
  applyDescriptorSets(node);
  node.assetBuffer->cmdBindVertexIndexBuffer(renderContext, commandBuffer, node.renderMask, node.vertexBinding);
  traverse(node);
  // FIXME - bind previous ?
  renderContext.setCurrentAssetBufferNode(previous);
}

void BuildCommandBufferVisitor::apply(AssetBufferDrawObject& node)
{
  LOG_ERROR << "BuildCommandBufferVisitor::apply(AssetBufferDrawObject& node) : " << node.getName() << std::endl;
  if (renderContext.currentAssetBufferNode == nullptr)
    return;
  applyDescriptorSets(node);
  renderContext.currentAssetBufferNode->assetBuffer->cmdDrawObject(renderContext.device, commandBuffer, renderContext.currentAssetBufferNode->renderMask, node.typeID, node.firstInstance, node.getDistanceToViewer());
}

void BuildCommandBufferVisitor::apply(Text& node)
{
  LOG_ERROR << "BuildCommandBufferVisitor::apply(Text& node) : " << node.getName() << std::endl;
  applyDescriptorSets(node);
  node.cmdDraw(renderContext, commandBuffer);
  traverse(node);
}

void BuildCommandBufferVisitor::applyDescriptorSets(Node& node)
{
  LOG_ERROR << "BuildCommandBufferVisitor::applyDescriptorSets(Node& node)" << std::endl;
  // FIXME : for now descriptor sets must be set below pipeline in a scene graph
  // It would be better if descriptor sets are collected and set in a moment of draw.
  if (renderContext.currentPipeline == nullptr)
    return;
  for (auto it = node.descriptorSetBegin(); it != node.descriptorSetEnd(); ++it )
  {
    commandBuffer->cmdBindDescriptorSets(renderContext, renderContext.currentPipeline->pipelineLayout.get(), it->first, it->second.get());
  }
}
