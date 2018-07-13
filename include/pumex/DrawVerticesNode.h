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
#include <memory>
#include <glm/glm.hpp>
#include <pumex/Export.h>
#include <pumex/DrawNode.h>

namespace pumex
{

class DeviceMemoryAllocator;
template <typename T> class Buffer;

// Class that draws user provided vertices and indices. BEWARE : be really sure what you are doing.

class PUMEX_EXPORT DrawVerticesNode : public DrawNode
{
public:
  DrawVerticesNode()                                   = delete;
  explicit DrawVerticesNode(const std::vector<VertexSemantic>& vertexSemantic, uint32_t vertexBinding, std::shared_ptr<DeviceMemoryAllocator> bufferAllocator, PerObjectBehaviour perObjectBehaviour = pbPerDevice, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage, bool sameDataPerObject = true );
  DrawVerticesNode(const DrawVerticesNode&)            = delete;
  DrawVerticesNode& operator=(const DrawVerticesNode&) = delete;
  DrawVerticesNode(DrawVerticesNode&&)                 = delete;
  DrawVerticesNode& operator=(DrawVerticesNode&&)      = delete;
  virtual ~DrawVerticesNode();

  void validate(const RenderContext& renderContext) override;
  void cmdDraw(const RenderContext& renderContext, CommandBuffer* commandBuffer) override;

  void setVertexIndexData(Surface* surface, const std::vector<float>& vertices, const std::vector<uint32_t>& indices);
  void setVertexIndexData(Device* device, const std::vector<float>& vertices, const std::vector<uint32_t>& indices);
  void setVertexIndexData(const std::vector<float>& vertices, const std::vector<uint32_t>& indices);

  std::vector<VertexSemantic>                      vertexSemantic;
protected:
  std::shared_ptr<Buffer<std::vector<float>>>      vertexBuffer;
  std::shared_ptr<Buffer<std::vector<uint32_t>>>   indexBuffer;
  uint32_t                                         vertexBinding;
  bool                                             registered = false;
};

}