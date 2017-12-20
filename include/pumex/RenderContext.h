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
#pragma once
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

class Surface;
class CommandPool;
class Device;	
class RenderPass;
class RenderOperation;
class Pipeline;
class AssetBufferNode;

class RenderContext
{
public:
  explicit RenderContext(Surface* surface);

  inline void             setRenderPass(RenderPass* renderPass);
  inline void             setSubpassIndex(uint32_t subpassIndex);
  inline void             setRenderOperation(RenderOperation* renderOperation);
  inline Pipeline*        setCurrentPipeline(Pipeline* pipeline);
  inline AssetBufferNode* setCurrentAssetBufferNode(AssetBufferNode* assetBufferNode);

  // elements of the context that are constant through visitor work
  Surface*         surface                = nullptr;
  VkSurfaceKHR     vkSurface              = VK_NULL_HANDLE;
  CommandPool*     commandPool            = nullptr;
  VkQueue          presentationQueue      = VK_NULL_HANDLE;
  Device*          device                 = nullptr;
  VkDevice         vkDevice               = VK_NULL_HANDLE;
  uint32_t         activeIndex            = 0;
  uint32_t         imageCount             = 1;

  // elements of the context that may change during visitor work
  RenderPass*      renderPass             = nullptr;
  uint32_t         subpassIndex           = 0;
  RenderOperation* renderOperation        = nullptr;
  Pipeline*        currentPipeline        = nullptr; // graphics pipeline or compute pipeline
  AssetBufferNode* currentAssetBufferNode = nullptr; // asset buffer
};

void             RenderContext::setRenderPass(RenderPass* rp)           { renderPass = rp; }
void             RenderContext::setSubpassIndex(uint32_t si)            { subpassIndex = si; }
void             RenderContext::setRenderOperation(RenderOperation* ro) { renderOperation = ro; }
Pipeline*        RenderContext::setCurrentPipeline(Pipeline* pipeline) 
{
  Pipeline* oldPipeline = currentPipeline;
  currentPipeline       = pipeline;
  return oldPipeline;
}
AssetBufferNode* RenderContext::setCurrentAssetBufferNode(AssetBufferNode* assetBufferNode)
{
  AssetBufferNode* oldAssetBufferNode = currentAssetBufferNode;
  currentAssetBufferNode = assetBufferNode;
  return oldAssetBufferNode;
}

}
