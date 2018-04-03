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

#include <pumex/Export.h>
#include <pumex/Asset.h>
#include <pumex/Node.h>
#include <pumex/GenericBuffer.h>

namespace pumex
{
	
class PUMEX_EXPORT AssetNode : public Node
{
public:
  AssetNode(std::shared_ptr<Asset> asset, std::shared_ptr<DeviceMemoryAllocator> bufferAllocator, uint32_t renderMask, uint32_t vertexBinding);

  void accept(NodeVisitor& visitor) override;
  void validate(const RenderContext& renderContext) override;

  void internalInvalidate();

  void cmdDraw(const RenderContext& renderContext, CommandBuffer* commandBuffer) const;


  std::shared_ptr<Asset> asset;
  uint32_t               renderMask;
  uint32_t               vertexBinding;
protected:
  bool                                                  geometryValid = false;
  std::shared_ptr<std::vector<float>>                   vertices;
  std::shared_ptr<std::vector<uint32_t>>                indices;
  std::shared_ptr<GenericBuffer<std::vector<float>>>    vertexBuffer;
  std::shared_ptr<GenericBuffer<std::vector<uint32_t>>> indexBuffer;
};

}