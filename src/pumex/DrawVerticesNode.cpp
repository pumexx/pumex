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
  std::lock_guard<std::mutex> lock(mutex);
  vertexBuffer->setData(surface, vertices);
  indexBuffer->setData(surface, indices);
  auto it = indexCount.find(surface->getID());
  if (it == end(indexCount) || it->second != indices.size())
    notifyCommandBuffers();
  indexCount[surface->getID()] = indices.size();
  invalidateNodeAndParents(surface);
}

void DrawVerticesNode::setVertexIndexData(Device* device, const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
{
  std::lock_guard<std::mutex> lock(mutex);
  vertexBuffer->setData(device, vertices);
  indexBuffer->setData(device, indices);
  auto it = indexCount.find(device->getID());
  if (it == end(indexCount) || it->second != indices.size())
    notifyCommandBuffers();
  indexCount[device->getID()] = indices.size();
  invalidateNodeAndParents();
}

void DrawVerticesNode::setVertexIndexData(const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
{
  std::lock_guard<std::mutex> lock(mutex);
  bool notifyCB = (indexBuffer->getData() == nullptr || indexBuffer->getData()->size() != indices.size());
  vertexBuffer->setData(vertices);
  indexBuffer->setData(indices);
  if(notifyCB)
    notifyCommandBuffers();
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
  uint32_t currentIndexCount = 0;
  if (vertexBuffer->getPerObjectBehaviour() == pbPerSurface)
  {
    auto it = indexCount.find(renderContext.surface->getID());
    if (it != end(indexCount))
      currentIndexCount = it->second;
  }
  else
  {
    auto it = indexCount.find(renderContext.device->getID());
    if (it != end(indexCount))
      currentIndexCount = it->second;
  }
  commandBuffer->cmdDrawIndexed(currentIndexCount, 1, 0, 0, 0);
}
