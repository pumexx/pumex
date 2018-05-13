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

#include <pumex/AssetNode.h>
#include <pumex/NodeVisitor.h>
#include <pumex/GenericBuffer.h>
#include <algorithm>

using namespace pumex;

AssetNode::AssetNode(std::shared_ptr<Asset> a, std::shared_ptr<DeviceMemoryAllocator> ba, uint32_t rm, uint32_t vb)
  : asset{ a }, renderMask{ rm }, vertexBinding{ vb }
{
  vertices     = std::make_shared<std::vector<float>>();
  indices      = std::make_shared<std::vector<uint32_t>>();
  vertexBuffer = std::make_shared<GenericBuffer<std::vector<float>>>(ba, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, pbPerDevice, swOnce);
  indexBuffer  = std::make_shared<GenericBuffer<std::vector<uint32_t>>>(ba, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, pbPerDevice, swOnce);
  vertexBuffer->set(vertices);
  indexBuffer->set(indices);
}

void AssetNode::accept(NodeVisitor& visitor)
{
  if (visitor.getMask() && mask)
  {
    visitor.push(this);
    visitor.apply(*this);
    visitor.pop();
  }
}

void AssetNode::validate(const RenderContext& renderContext)
{
  if (geometryValid)
  {
    vertexBuffer->validate(renderContext);
    indexBuffer->validate(renderContext);
    return;
  }
  vertices->resize(0);
  indices->resize(0);
  VkDeviceSize vertexCount = 0;

  for (unsigned int i = 0; i < asset->geometries.size(); ++i)
  {
    if (asset->geometries[i].renderMask != renderMask)
      continue;

    copyAndConvertVertices(*vertices, asset->geometries[i].semantic, asset->geometries[i].vertices, asset->geometries[i].semantic);
    std::transform(begin(asset->geometries[i].indices), end(asset->geometries[i].indices), std::back_inserter(*indices), [vertexCount](uint32_t value)->uint32_t { return value + vertexCount; });
    vertexCount += asset->geometries[i].getVertexCount();
  }
  vertexBuffer->invalidate();
  indexBuffer->invalidate();

  vertexBuffer->validate(renderContext);
  indexBuffer->validate(renderContext);
  geometryValid = true;
}

void AssetNode::cmdDraw(const RenderContext& renderContext, CommandBuffer* commandBuffer) const
{
  VkBuffer vBuffer = vertexBuffer->getHandleBuffer(renderContext);
  VkBuffer iBuffer = indexBuffer->getHandleBuffer(renderContext);
  commandBuffer->addSource(vertexBuffer.get());
  commandBuffer->addSource(indexBuffer.get());
  VkDeviceSize offsets = 0;
  vkCmdBindVertexBuffers(commandBuffer->getHandle(), vertexBinding, 1, &vBuffer, &offsets);
  vkCmdBindIndexBuffer(commandBuffer->getHandle(), iBuffer, 0, VK_INDEX_TYPE_UINT32);
  commandBuffer->cmdDrawIndexed(indices->size(), 1, 0, 0, 0);
}
