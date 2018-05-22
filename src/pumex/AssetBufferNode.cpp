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

#include <pumex/AssetBufferNode.h>
#include <pumex/NodeVisitor.h>
#include <pumex/MaterialSet.h>
#include <pumex/Descriptor.h>
#include <pumex/utils/Log.h>

using namespace pumex;

AssetBufferNode::AssetBufferNode(std::shared_ptr<AssetBuffer> ab, std::shared_ptr<MaterialSet> ms, uint32_t rm, uint32_t vb)
  : assetBuffer{ ab }, materialSet{ ms }, renderMask { rm }, vertexBinding{ vb }
{
}

void AssetBufferNode::accept(NodeVisitor& visitor)
{
  if (visitor.getMask() && mask)
  {
    visitor.push(this);
    visitor.apply(*this);
    visitor.pop();
  }
}

void AssetBufferNode::validate(const RenderContext& renderContext)
{
  if (!registered)
  {
    assetBuffer->addNodeOwner(std::dynamic_pointer_cast<Node>(shared_from_this()));
//    assetBuffer->addNodeOwner(std::dynamic_pointer_cast<Node>(shared_from_this()));
    registered = true;
  }
  bool needNotify = false;
  if(assetBuffer.get() != nullptr)
    needNotify |= assetBuffer->validate(renderContext);
  if (materialSet.get() != nullptr)
    materialSet->validate(renderContext);
  if (needNotify)
    notifyCommandBuffers();
}

AssetBufferFilterNode::AssetBufferFilterNode(std::shared_ptr<AssetBuffer> ab, std::shared_ptr<DeviceMemoryAllocator> buffersAllocator, std::function<void(uint32_t,size_t)> fuo)
  : assetBuffer{ ab }, funcUpdateOutput{ fuo }
{
  auto masks = assetBuffer->getRenderMasks();
  for (const auto& m : masks)
    perRenderMaskData[m] = PerRenderMaskData(buffersAllocator);
}

void AssetBufferFilterNode::accept(NodeVisitor& visitor)
{
  if (visitor.getMask() && mask)
  {
    visitor.push(this);
    visitor.apply(*this);
    visitor.pop();
  }
}

void AssetBufferFilterNode::validate(const RenderContext& renderContext)
{
  if (!registered)
  {
    assetBuffer->addNodeOwner(std::dynamic_pointer_cast<Node>(shared_from_this()));
    registered = true;
  }
  bool needNotify = false;
  if (assetBuffer.get() != nullptr)
    needNotify |= assetBuffer->validate(renderContext);

  for (auto& prm : perRenderMaskData)
    prm.second.drawIndexedIndirectBuffer->validate(renderContext);

  if (needNotify)
    notifyCommandBuffers();
}

void AssetBufferFilterNode::setTypeCount(const std::vector<size_t>& tc)
{
  typeCount = tc;
  for (auto& prm : perRenderMaskData)
  {
    PerRenderMaskData& rmData = prm.second;

    std::vector<uint32_t> typeOfGeometry;
    assetBuffer->prepareDrawCommands(prm.first, (*rmData.drawIndexedIndirectCommands), typeOfGeometry);

    std::vector<uint32_t> offsets;
    for (uint32_t i = 0; i < typeOfGeometry.size(); ++i)
      offsets.push_back(typeCount[typeOfGeometry[i]]);

    size_t offsetSum = 0;
    for (uint32_t i = 0; i < offsets.size(); ++i)
    {
      uint32_t tmp = offsetSum;
      offsetSum += offsets[i];
      offsets[i] = tmp;
      (*rmData.drawIndexedIndirectCommands)[i].firstInstance = tmp;
    }
    rmData.drawIndexedIndirectBuffer->invalidateData();
    rmData.maxOutputObjects = offsetSum;

    funcUpdateOutput(prm.first, rmData.maxOutputObjects);
  }
  invalidateNodeAndParents();
}

std::shared_ptr<Buffer<std::vector<DrawIndexedIndirectCommand>>> AssetBufferFilterNode::getDrawIndexedIndirectBuffer(uint32_t renderMask)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perRenderMaskData.find(renderMask);
  CHECK_LOG_THROW(it == std::end(perRenderMaskData), "AssetBufferInstancedResults::getResults() attempting to get a buffer for nonexisting render mask");
  return it->second.drawIndexedIndirectBuffer;
}

size_t AssetBufferFilterNode::getMaxOutputObjects(uint32_t renderMask)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perRenderMaskData.find(renderMask);
  CHECK_LOG_THROW(it == std::end(perRenderMaskData), "AssetBufferInstancedResults::getOffsetValues() attempting to get a buffer for nonexisting render mask");
  return it->second.maxOutputObjects;
}

uint32_t AssetBufferFilterNode::getDrawCount(uint32_t renderMask)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perRenderMaskData.find(renderMask);
  CHECK_LOG_THROW(it == std::end(perRenderMaskData), "AssetBufferInstancedResults::getDrawCount() attempting to get a draw count for nonexisting render mask");
  return it->second.drawIndexedIndirectCommands->size();
}

AssetBufferFilterNode::PerRenderMaskData::PerRenderMaskData(std::shared_ptr<DeviceMemoryAllocator> allocator)
{
  drawIndexedIndirectCommands = std::make_shared<std::vector<DrawIndexedIndirectCommand>>();
  drawIndexedIndirectBuffer   = std::make_shared<Buffer<std::vector<DrawIndexedIndirectCommand>>>(drawIndexedIndirectCommands, allocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, pbPerSurface, swForEachImage);
  maxOutputObjects            = 0;
}

AssetBufferDrawObject::AssetBufferDrawObject(uint32_t tid, uint32_t fi)
  : typeID{ tid }, firstInstance{ fi }
{
}

void AssetBufferDrawObject::accept(NodeVisitor& visitor)
{
  if (visitor.getMask() && mask)
  {
    visitor.push(this);
    visitor.apply(*this);
    visitor.pop();
  }
}

void AssetBufferDrawObject::validate(const RenderContext& renderContext)
{
}

float AssetBufferDrawObject::getDistanceToViewer() const
{
  // FIXME - we need to pass Camera object somehow here
  // For now we will return constant value
  return 10.0f;
}

AssetBufferIndirectDrawObjects::AssetBufferIndirectDrawObjects(std::shared_ptr<Buffer<std::vector<DrawIndexedIndirectCommand>>> dc)
  : drawCommands{ dc }
{
}

void AssetBufferIndirectDrawObjects::accept(NodeVisitor& visitor)
{
  if (visitor.getMask() && mask)
  {
    visitor.push(this);
    visitor.apply(*this);
    visitor.pop();
  }
}

void AssetBufferIndirectDrawObjects::validate(const RenderContext& renderContext)
{
  if (!registered)
  {
    drawCommands->addCommandBufferSource(shared_from_this());
    registered = true;
  }
  drawCommands->validate(renderContext);
}

std::shared_ptr<Buffer<std::vector<DrawIndexedIndirectCommand>>> AssetBufferIndirectDrawObjects::getDrawCommands()
{
  return drawCommands;
}

