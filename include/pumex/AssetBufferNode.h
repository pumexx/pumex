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
#include <functional>
#include <pumex/AssetBuffer.h>
#include <pumex/Export.h>
#include <pumex/Node.h>
#include <pumex/DrawNode.h>

namespace pumex
{

class MaterialSet;

// Node class that stores a pointer to AssetBuffer for drawing shaders ( shaders that draw objects using instance data ). There may be many such objects pointing at the same AssetBuffer

class PUMEX_EXPORT AssetBufferNode : public Group
{
public:
  AssetBufferNode(std::shared_ptr<AssetBuffer> assetBuffer, std::shared_ptr<MaterialSet> materialSet, uint32_t renderMask, uint32_t vertexBinding);

  void accept(NodeVisitor& visitor) override;
  void validate(const RenderContext& renderContext) override;

  std::shared_ptr<AssetBuffer> assetBuffer;
  std::shared_ptr<MaterialSet> materialSet;
  uint32_t                     renderMask;
  uint32_t                     vertexBinding;
  bool                         registered = false;
};

// Node class that stores a pointer to AssetBuffer for compute shaders ( shaders that filter instances for later rendering )

class PUMEX_EXPORT AssetBufferFilterNode : public Group
{
public:
  AssetBufferFilterNode(std::shared_ptr<AssetBuffer> assetBuffer, std::shared_ptr<DeviceMemoryAllocator> buffersAllocator);

  void                                                             accept(NodeVisitor& visitor) override;
  void                                                             validate(const RenderContext& renderContext) override;

  void                                                             setTypeCount(const std::vector<size_t>& typeCount);
  inline void                                                      setEventResizeOutputs(std::function<void(uint32_t, size_t)> event);

  std::shared_ptr<Buffer<std::vector<DrawIndexedIndirectCommand>>> getDrawIndexedIndirectBuffer(uint32_t renderMask);
  size_t                                                           getMaxOutputObjects(uint32_t renderMask);
  uint32_t                                                         getDrawCount(uint32_t renderMask);

protected:
  std::shared_ptr<AssetBuffer>                                     assetBuffer;
  std::vector<size_t>                                              typeCount;
  std::function<void(uint32_t, size_t)>                            eventResizeOutputs;

  inline void                                                      onEventResizeOutputs(uint32_t mask, size_t instanceCount);

  struct PerRenderMaskData
  {
    PerRenderMaskData() = default;
    PerRenderMaskData(std::shared_ptr<DeviceMemoryAllocator> allocator);

    std::shared_ptr<std::vector<DrawIndexedIndirectCommand>>         drawIndexedIndirectCommands;
    std::shared_ptr<Buffer<std::vector<DrawIndexedIndirectCommand>>> drawIndexedIndirectBuffer;
    size_t                                                           maxOutputObjects;
  };
  std::unordered_map<uint32_t, PerRenderMaskData>                    perRenderMaskData;

  bool                                                               registered = false;
};

void AssetBufferFilterNode::setEventResizeOutputs(std::function<void(uint32_t, size_t)> event) { eventResizeOutputs = event; }
void AssetBufferFilterNode::onEventResizeOutputs(uint32_t mask, size_t instanceCount) { if (eventResizeOutputs != nullptr)  eventResizeOutputs(mask, instanceCount); }

// Node class that draws single object registered in AssetBufferNode
class PUMEX_EXPORT AssetBufferDrawObject : public DrawNode
{
public:
  AssetBufferDrawObject(uint32_t typeID, uint32_t firstInstance = 0);

  void validate(const RenderContext& renderContext) override;

  void cmdDraw(const RenderContext& renderContext, CommandBuffer* commandBuffer) override;

  float getDistanceToViewer() const;

  uint32_t typeID;
  uint32_t firstInstance;
};

// Node class that draws series of objects registered in AssetBufferNode using cmdDrawIndexedIndirect - needs a buffer to work
class PUMEX_EXPORT AssetBufferIndirectDrawObjects : public DrawNode
{
public:
  AssetBufferIndirectDrawObjects(std::shared_ptr<AssetBufferFilterNode> filterNode, uint32_t renderMask);

  void validate(const RenderContext& renderContext) override;
  void cmdDraw(const RenderContext& renderContext, CommandBuffer* commandBuffer) override;

  uint32_t                                                         renderMask;
protected:
  std::shared_ptr<Buffer<std::vector<DrawIndexedIndirectCommand>>> drawCommands;
  bool                                                             registered = false;
};

}
