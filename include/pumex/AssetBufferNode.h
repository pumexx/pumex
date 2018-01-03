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

#include <pumex/AssetBuffer.h>
#include <pumex/MaterialSet.h>
#include <pumex/Export.h>
#include <pumex/Node.h>

namespace pumex
{
	
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
};

// class that draws single object registered in AssetBufferNode
class PUMEX_EXPORT AssetBufferDrawObject : public Node
{
public:
  AssetBufferDrawObject(uint32_t typeID, uint32_t firstInstance = 0);

  void accept(NodeVisitor& visitor) override;
  void validate(const RenderContext& renderContext) override;

  float getDistanceToViewer() const;

  uint32_t typeID;
  uint32_t firstInstance;
};

class PUMEX_EXPORT AssetNode : public Node
{
public:
  AssetNode(std::shared_ptr<pumex::Asset> asset, uint32_t renderMask, uint32_t vertexBinding);

  void accept(NodeVisitor& visitor) override;
  void validate(const RenderContext& renderContext) override;

  std::shared_ptr<pumex::Asset> asset;
  uint32_t                      renderMask;
  uint32_t                      vertexBinding;
};


}