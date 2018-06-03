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
#include <vector>
#include <pumex/Export.h>
#include <vulkan/vulkan.h>
#include <pumex/RenderContext.h>

namespace pumex
{

enum PerObjectBehaviour { pbPerDevice, pbPerSurface };
enum SwapChainImageBehaviour { swOnce, swForEachImage };

template<typename T, typename U>
struct PerObjectData
{
  PerObjectData(const RenderContext& renderContext, SwapChainImageBehaviour scib);
  PerObjectData(VkDevice device, VkSurfaceKHR surface, uint32_t activeCount, SwapChainImageBehaviour scib);
  void resize(uint32_t ac);
  void invalidate();
  bool anyValid();

  VkDevice                device;
  VkSurfaceKHR            surface;
  std::vector<char>       valid;
  std::vector<T>          data;
  U                       commonData;
  SwapChainImageBehaviour swapChainImageBehaviour;
};

inline void* getKey(const RenderContext& renderContext, const PerObjectBehaviour& pob);
uint32_t getKeyID(const RenderContext& renderContext, const PerObjectBehaviour& pob);

template<typename T, typename U>
PerObjectData<T,U>::PerObjectData(const RenderContext& renderContext, SwapChainImageBehaviour scib)
  : device{ renderContext.vkDevice }, surface{ renderContext.vkSurface }, commonData(), swapChainImageBehaviour{ scib }
{
  resize(swapChainImageBehaviour==swForEachImage ? renderContext.imageCount : 1);
}

template<typename T, typename U>
PerObjectData<T,U>::PerObjectData(VkDevice d, VkSurfaceKHR s, uint32_t ac, SwapChainImageBehaviour scib)
  : device{ d }, surface{ s }, commonData(), swapChainImageBehaviour{ scib }
{
  resize(ac);
}

template<typename T, typename U>
void PerObjectData<T,U>::resize(uint32_t ac)
{
  uint32_t newSize = (swapChainImageBehaviour == swForEachImage) ? ac : 1;
  valid.resize(newSize, false);
  data.resize(newSize, T());
}

template<typename T, typename U>
void PerObjectData<T,U>::invalidate()
{
  std::fill(begin(valid), end(valid), false);
}

template<typename T, typename U>
bool PerObjectData<T, U>::anyValid()
{
  for (uint32_t i = 0; i < valid.size(); ++i)
  {
    if (valid[i])
      return true;
  }
  return false;
}

void* getKey(const RenderContext& renderContext, const PerObjectBehaviour& pob)
{
  switch (pob)
  {
  case pbPerDevice:  return (void*)renderContext.vkDevice;
  case pbPerSurface: return (void*)renderContext.vkSurface;
  }
  return nullptr;
}

}