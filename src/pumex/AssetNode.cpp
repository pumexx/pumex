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
#include <pumex/MemoryBuffer.h>
#include <pumex/NodeVisitor.h>
#include <algorithm>
#include <iterator>

using namespace pumex;

AssetNode::AssetNode(std::shared_ptr<DeviceMemoryAllocator> ba, uint32_t rm, uint32_t vb)
  : DrawNode(), renderMask{ rm }, vertexBinding{ vb }
{
  vertices = std::make_shared<std::vector<float>>();
  indices = std::make_shared<std::vector<uint32_t>>();

  vertexBuffer = std::make_shared<Buffer<std::vector<float>>>(vertices, ba, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, pbPerDevice, swOnce);
  indexBuffer = std::make_shared<Buffer<std::vector<uint32_t>>>(indices, ba, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, pbPerDevice, swOnce);
}

AssetNode::AssetNode(std::shared_ptr<Asset> asset, std::shared_ptr<DeviceMemoryAllocator> ba, uint32_t rm, uint32_t vb)
  : DrawNode(), renderMask{ rm }, vertexBinding{ vb }
{
  vertices     = std::make_shared<std::vector<float>>();
  indices      = std::make_shared<std::vector<uint32_t>>();

  vertexBuffer = std::make_shared<Buffer<std::vector<float>>>(vertices, ba, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, pbPerDevice, swOnce);
  indexBuffer  = std::make_shared<Buffer<std::vector<uint32_t>>>(indices, ba, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, pbPerDevice, swOnce);

  setAsset(asset);
}

void AssetNode::setAsset(std::shared_ptr<Asset> asset)
{
  vertices->clear();
  indices->clear();
  VkDeviceSize vertexCount = 0;
  for (unsigned int i = 0; i < asset->geometries.size(); ++i)
  {
    if (asset->geometries[i].renderMask != renderMask)
      continue;

    copyAndConvertVertices(*vertices, asset->geometries[i].semantic, asset->geometries[i].vertices, asset->geometries[i].semantic);
    std::transform(begin(asset->geometries[i].indices), end(asset->geometries[i].indices), std::back_inserter(*indices), [vertexCount](uint32_t value)->uint32_t { return value + vertexCount; });
    vertexCount += asset->geometries[i].getVertexCount();
  }
  invalidateNodeAndParents();
  notifyCommandBuffers();
  vertexBuffer->invalidateData();
  indexBuffer->invalidateData();
}


void AssetNode::validate(const RenderContext& renderContext)
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

void AssetNode::cmdDraw(const RenderContext& renderContext, CommandBuffer* commandBuffer)
{
  std::lock_guard<std::mutex> lock(mutex);
  commandBuffer->addSource(this);
  VkBuffer vBuffer = vertexBuffer->getHandleBuffer(renderContext);
  VkBuffer iBuffer = indexBuffer->getHandleBuffer(renderContext);
  VkDeviceSize offsets = 0;
  vkCmdBindVertexBuffers(commandBuffer->getHandle(), vertexBinding, 1, &vBuffer, &offsets);
  vkCmdBindIndexBuffer(commandBuffer->getHandle(), iBuffer, 0, VK_INDEX_TYPE_UINT32);
  commandBuffer->cmdDrawIndexed(indices->size(), 1, 0, 0, 0);
}
