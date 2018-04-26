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
class ImageView;

struct PUMEX_EXPORT ImageSubresourceRange
{
  ImageSubresourceRange(VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount);

  VkImageSubresourceRange getSubresource();

  VkImageAspectFlags    aspectMask;
  uint32_t              baseMipLevel;
  uint32_t              levelCount;
  uint32_t              baseArrayLayer;
  uint32_t              layerCount;
};

// pumex::Texture class stores Vulkan images per sufrace or per device ( according to user's needs )
// Class uses gli::texture to store texture data on CPU
// Texture may contain usual textures, texture arrays, texture cubes, arrays of texture cubes etc, but cubes were not tested in real life ( be aware )

class PUMEX_EXPORT Texture
{
public:
  Texture()                          = delete;
  // create single texture and clear it with specific value
  explicit Texture(const ImageTraits& imageTraits, std::shared_ptr<DeviceMemoryAllocator> allocator, const glm::vec4& initValue, PerObjectBehaviour perObjectBehaviour = pbPerDevice, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage);
  // create single texture and load it with provided data ( gli::texture )
  explicit Texture(std::shared_ptr<gli::texture> texture, std::shared_ptr<DeviceMemoryAllocator> allocator, VkImageUsageFlags usage, PerObjectBehaviour perObjectBehaviour = pbPerDevice, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage);
  Texture(const Texture&)            = delete;
  Texture& operator=(const Texture&) = delete;
  virtual ~Texture();

  Image*                                getImage(const RenderContext& renderContext) const;
  inline const ImageTraits&             getImageTraits() const;
  inline const PerObjectBehaviour&      getPerObjectBehaviour() const;
  inline const SwapChainImageBehaviour& getSwapChainImageBehaviour() const;

  void                                  validate(const RenderContext& renderContext);
  void                                  invalidate();

  void                                  setLayer(uint32_t layer, std::shared_ptr<gli::texture> tex);
  ImageSubresourceRange                 getFullImageRange();

  void                                  addImageView( std::shared_ptr<ImageView> imageView );
protected:
  struct TextureInternal
  {
    std::shared_ptr<Image> image;
  };
  std::unordered_map<void*, PerObjectData<TextureInternal>> perObjectData;
  mutable std::mutex                                        mutex;
  PerObjectBehaviour                                        perObjectBehaviour;
  SwapChainImageBehaviour                                   swapChainImageBehaviour;
  ImageTraits                                               imageTraits;
  std::shared_ptr<gli::texture>                             texture;
  std::shared_ptr<DeviceMemoryAllocator>                    allocator;
  VkClearValue                                              initValue;
  std::vector<std::weak_ptr<ImageView>>                     imageViews;
  uint32_t                                                  activeCount;

  void buildImageTraits(VkImageUsageFlags usage);
  void invalidateImageViews();
};

class PUMEX_EXPORT ImageView : public std::enable_shared_from_this<ImageView>
{
public:
  ImageView()                            = delete;
  ImageView(std::shared_ptr<Texture> texture, const ImageSubresourceRange& subresourceRange, VkImageViewType viewType, VkFormat format = VK_FORMAT_UNDEFINED, const gli::swizzles& swizzles = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA));
  ImageView(const ImageView&)            = delete;
  ImageView& operator=(const ImageView&) = delete;
  virtual ~ImageView();

  VkImage      getHandleImage(const RenderContext& renderContext) const;
  VkImageView  getImageView(const RenderContext& renderContext) const;

  void         validate(const RenderContext& renderContext);
  void         invalidate();

  void         addResource(std::shared_ptr<Resource> resource);

  std::shared_ptr<Texture> texture;
  ImageSubresourceRange    subresourceRange;
  VkImageViewType          viewType;
  VkFormat                 format;
  gli::swizzles            swizzles;
protected:
  struct ImageViewInternal
  {
    ImageViewInternal()
      : imageView{VK_NULL_HANDLE}
    {}
    VkImageView imageView;
  };
  mutable std::mutex                                          mutex;
  std::vector<std::weak_ptr<Resource>>                        resources;
  std::unordered_map<void*, PerObjectData<ImageViewInternal>> perObjectData;
  uint32_t                                                    activeCount;

  void invalidateResources();

};

const ImageTraits&             Texture::getImageTraits() const { return imageTraits; }
const PerObjectBehaviour&      Texture::getPerObjectBehaviour() const { return perObjectBehaviour; }
const SwapChainImageBehaviour& Texture::getSwapChainImageBehaviour() const { return swapChainImageBehaviour; }

}

