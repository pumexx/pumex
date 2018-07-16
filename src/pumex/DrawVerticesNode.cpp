//
// Copyright(c) 2017-2018 Pawe³ Ksiê¿opolski ( pumexx )
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

#include <pumex/DrawVerticesNode.h>
#include <pumex/NodeVisitor.h>
#include <pumex/utils/Log.h>
#include <pumex/Surface.h>
#include <pumex/MemoryBuffer.h>

using namespace pumex;

DrawVerticesNode::DrawVerticesNode(const std::vector<VertexSemantic>& vs, uint32_t vb, std::shared_ptr<DeviceMemoryAllocator> ba, PerObjectBehaviour pob, SwapChainImageBehaviour scib, bool sdpo)
  : DrawNode(), vertexSemantic(vs), vertexBinding{ vb }
{
  vertexBuffer = std::make_shared<Buffer<std::vector<float>>>(ba, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, pob, scib, sdpo);
  indexBuffer  = std::make_shared<Buffer<std::vector<uint32_t>>>(ba, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, pob, scib, sdpo);
}

DrawVerticesNode::~DrawVerticesNode()
{
}

void DrawVerticesNode::setVertexIndexData(Surface* surface, const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
{
  vertexBuffer->setData(surface, vertices);
  indexBuffer->setData(surface, indices);
  invalidateNodeAndParents(surface);
}

void DrawVerticesNode::setVertexIndexData(Device* device, const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
{
  vertexBuffer->setData(device, vertices);
  indexBuffer->setData(device, indices);
  invalidateNodeAndParents();
}

void DrawVerticesNode::setVertexIndexData(const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
{
  vertexBuffer->setData(vertices);
  indexBuffer->setData(indices);
  invalidateNodeAndParents();
}

void DrawVerticesNode::validate(const RenderContext& renderContext)
{
  if (!registered)
  {
    vertexBuffer->addCommandBufferSource(shared_from_this());
    indexBuffer->addCommandBufferSource(shared_from_this());
    registered = true;
  }
  vertexBuffer->validate(renderContext);
  indexBuffer->validate(renderContext);
}

void DrawVerticesNode::cmdDraw(const RenderContext& renderContext, CommandBuffer* commandBuffer)
{
  std::lock_guard<std::mutex> lock(mutex);
  commandBuffer->addSource(this);
  VkBuffer vBuffer = vertexBuffer->getHandleBuffer(renderContext);
  VkBuffer iBuffer = indexBuffer->getHandleBuffer(renderContext);
  VkDeviceSize offsets = 0;
  vkCmdBindVertexBuffers(commandBuffer->getHandle(), vertexBinding, 1, &vBuffer, &offsets);
  vkCmdBindIndexBuffer(commandBuffer->getHandle(), iBuffer, 0, VK_INDEX_TYPE_UINT32);
  uint32_t drawSize = indexBuffer->getDataSizeRC(renderContext) / sizeof(uint32_t);
  commandBuffer->cmdDrawIndexed(drawSize, 1, 0, 0, 0);
}
