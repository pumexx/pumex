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

#pragma once
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/Resource.h>
#include <pumex/Image.h>

namespace pumex
{

class RenderContext;

// Uses gli::texture to hold texture on CPU
// Texture may contain usual textures, texture arrays, texture cubes, arrays of texture cubes etc, but cubes were not tested in real life ( be aware )
// Texture may be used in a descriptor as as sampled image, combined image sampler and image store
// Class stores information about images PER SURFACE. Additionally it also stores a VkSampler
class PUMEX_EXPORT TexturePerSurface : public Resource
{
public:
  TexturePerSurface()                                    = delete;
  // create single texture and clear it with specific value
  explicit TexturePerSurface(const ImageTraits& imageTraits, std::shared_ptr<DeviceMemoryAllocator> allocator, VkClearValue initValue, Resource::SwapChainImageBehaviour swapChainImageBehaviour = Resource::ForEachSwapChainImage);
  explicit TexturePerSurface(const ImageTraits& imageTraits, const SamplerTraits& samplerTraits, std::shared_ptr<DeviceMemoryAllocator> allocator, VkClearValue initValue, Resource::SwapChainImageBehaviour swapChainImageBehaviour = Resource::ForEachSwapChainImage);
  // create single texture and load it with provided data ( gli::texture )
  explicit TexturePerSurface(std::shared_ptr<gli::texture> texture, std::shared_ptr<DeviceMemoryAllocator> allocator, VkImageUsageFlags usage, Resource::SwapChainImageBehaviour swapChainImageBehaviour = Resource::ForEachSwapChainImage);
  explicit TexturePerSurface(std::shared_ptr<gli::texture> texture, const SamplerTraits& samplerTraits, std::shared_ptr<DeviceMemoryAllocator> allocator, VkImageUsageFlags usage, Resource::SwapChainImageBehaviour swapChainImageBehaviour = Resource::ForEachSwapChainImage);
  TexturePerSurface(const TexturePerSurface&)            = delete;
  TexturePerSurface& operator=(const TexturePerSurface&) = delete;

  virtual ~TexturePerSurface();

  Image*                      getHandleImage(const RenderContext& renderContext) const;
  VkSampler                   getHandleSampler(const RenderContext& renderContext) const;
  inline const ImageTraits&   getImageTraits() const;
  inline bool                 usesSampler() const;
  inline const SamplerTraits& getSamplerTraits() const;

  void                        validate(const RenderContext& renderContext) override;
  void                        invalidate() override;
  DescriptorSetValue          getDescriptorSetValue(const RenderContext& renderContext) override;

  void setLayer(uint32_t layer, std::shared_ptr<gli::texture> tex);

private:
  struct PerSurfaceData
  {
    PerSurfaceData(uint32_t ac, VkDevice d)
      : device{ d }
    {
      resize(ac);
    }
    void resize(uint32_t ac)
    {
      valid.resize(ac, false);
      image.resize(ac, nullptr);
      sampler.resize(ac, VK_NULL_HANDLE);
    }
    void invalidate()
    {
      std::fill(begin(valid), end(valid), false);
    }

    VkDevice                            device;
    std::vector<bool>                   valid;
    std::vector<std::shared_ptr<Image>> image;
    std::vector<VkSampler>              sampler;
  };
  void buildImageTraits(VkImageUsageFlags usage);

  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
  ImageTraits                                      imageTraits;
  bool                                             useSampler;
  SamplerTraits                                    samplerTraits;
  std::shared_ptr<gli::texture>                    texture;
  VkClearValue                                     initValue;
  std::shared_ptr<DeviceMemoryAllocator>           allocator;
  uint32_t                                         activeCount = 1;
};

const ImageTraits&   TexturePerSurface::getImageTraits() const   { return imageTraits; }
bool                 TexturePerSurface::usesSampler() const      { return useSampler;  }
const SamplerTraits& TexturePerSurface::getSamplerTraits() const { return samplerTraits; }

}

