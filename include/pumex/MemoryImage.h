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
#include <list>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/MemoryObject.h>
#include <pumex/PerObjectData.h>
#include <pumex/Image.h>

namespace pumex
{

class Resource;
class RenderContext;
class CommandBuffer;
class CommandBufferSource;
class ImageView;

// struct defining subresource range for image
struct PUMEX_EXPORT ImageSubresourceRange
{
  ImageSubresourceRange();
  ImageSubresourceRange(VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount);

  VkImageSubresourceRange getSubresource() const;
  bool contains(const ImageSubresourceRange& subRange) const;

  VkImageAspectFlags    aspectMask;
  uint32_t              baseMipLevel;
  uint32_t              levelCount;
  uint32_t              baseArrayLayer;
  uint32_t              layerCount;
};

// pumex::MemoryImage class stores Vulkan images per sufrace or per device ( according to user's needs )
// Class uses gli::texture to store texture data on CPU
// MemoryImage may contain 1D, 2D and 3D textures, texture arrays, texture cubes, arrays of texture cubes etc, but cubes were not tested in real life ( be aware )
class PUMEX_EXPORT MemoryImage : public MemoryObject
{
public:
  MemoryImage()                              = delete;
  explicit MemoryImage(const ImageTraits& imageTraits, std::shared_ptr<DeviceMemoryAllocator> allocator, VkImageAspectFlags aspectMask, PerObjectBehaviour perObjectBehaviour = pbPerDevice, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage, bool sameTraitsPerObject = true, bool useSetImageMethods = true);
  explicit MemoryImage(std::shared_ptr<gli::texture> texture, std::shared_ptr<DeviceMemoryAllocator> allocator, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT, PerObjectBehaviour perObjectBehaviour = pbPerDevice);
  MemoryImage(const MemoryImage&)            = delete;
  MemoryImage& operator=(const MemoryImage&) = delete;
  MemoryImage(MemoryImage&&)                 = delete;
  MemoryImage& operator=(MemoryImage&&)      = delete;
  virtual ~MemoryImage();

  MemoryImage*  asMemoryImage() override;

  void setImageTraits(const ImageTraits& traits);
  void setImageTraits(Surface* surface, const ImageTraits& traits);
  void setImageTraits(Device* device, const ImageTraits& traits);

  void invalidateImage();
  void setImage(Surface* surface, std::shared_ptr<gli::texture> tex);
  void setImage(Device* device, std::shared_ptr<gli::texture> tex);
  void setImageLayer(uint32_t layer, std::shared_ptr<gli::texture> tex);
  // use outside created images ( method created to catch swapchain images )
  void setImages(Surface* surface, std::vector<std::shared_ptr<Image>>& images);
  void setImages(Device* device, std::vector<std::shared_ptr<Image>>& images);

  void clearImages(const glm::vec4& clearValue, const ImageSubresourceRange& range = ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS));
  void clearImage(Surface* surface, const glm::vec4& clearValue, const ImageSubresourceRange& range = ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS));
  void clearImage(Device* device, const glm::vec4& clearValue, const ImageSubresourceRange& range = ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS));

  Image*                                        getImage(const RenderContext& renderContext) const;
  inline const ImageTraits&                     getImageTraits() const;
  inline VkImageAspectFlags                     getAspectMask() const;
  inline const PerObjectBehaviour&              getPerObjectBehaviour() const;
  inline const SwapChainImageBehaviour&         getSwapChainImageBehaviour() const;
  inline std::shared_ptr<DeviceMemoryAllocator> getAllocator() const;
  inline std::shared_ptr<gli::texture>          getTexture() const;

  void                                          validate(const RenderContext& renderContext);

  ImageSubresourceRange                         getFullImageRange();

  void                                          addCommandBufferSource(std::shared_ptr<CommandBufferSource> cbSource);
  void                                          notifyCommandBufferSources(const RenderContext& renderContext);

  void                                          addImageView( std::shared_ptr<ImageView> imageView );
  void                                          notifyImageViews(const RenderContext& renderContext, const ImageSubresourceRange& range);
  void                                          invalidateImageViews();

  struct MemoryImageInternal
  {
    std::shared_ptr<Image> image;
  };
  // struct that defines all operations that may be performed on that Texture ( set new image traits, clear it, set new data )
  struct Operation
  {
    enum Type { SetImageTraits, SetImage, NotifyImageViews, ClearImage };
    Operation(MemoryImage* o, Type t, const ImageSubresourceRange& r, uint32_t ac)
      : owner{ o }, type{ t }, imageRange{ r }
    {
      resize(ac);
    }
    virtual ~Operation()
    {
    }
    void resize(uint32_t ac)
    {
      updated.resize(ac, false);
    }
    bool allUpdated()
    {
      for (auto& u : updated)
        if (!u)
          return false;
      return true;
    }
    // perform() should return true when it added commands to commandBuffer
    virtual bool perform(const RenderContext& renderContext, MemoryImageInternal& internals, std::shared_ptr<CommandBuffer> commandBuffer) = 0;
    virtual void releaseResources(const RenderContext& renderContext)
    {
    }

    MemoryImage*          owner;
    Type                  type;
    ImageSubresourceRange imageRange;
    std::vector<char>     updated;
  };
protected:
  struct MemoryImageLoadData
  {
    std::list<std::shared_ptr<Operation>> imageOperations;
  };
  typedef PerObjectData<MemoryImageInternal, MemoryImageLoadData> MemoryImageData;

  std::unordered_map<uint32_t, MemoryImageData>   perObjectData;
  mutable std::mutex                              mutex;
  PerObjectBehaviour                              perObjectBehaviour;
  SwapChainImageBehaviour                         swapChainImageBehaviour;
  bool                                            sameTraitsPerObject;
  ImageTraits                                     imageTraits;
  std::shared_ptr<gli::texture>                   texture;
  std::shared_ptr<DeviceMemoryAllocator>          allocator;
  VkImageAspectFlags                              aspectMask;
  uint32_t                                        activeCount;
  // objects that may own a texture and must be informed when some changes happen
  std::vector<std::weak_ptr<CommandBufferSource>> commandBufferSources;
  std::vector<std::weak_ptr<ImageView>>           imageViews;

  void internalSetImageTraits(uint32_t key, VkDevice device, VkSurfaceKHR surface, const ImageTraits& traits, VkImageAspectFlags aMask);
  void internalSetImage(uint32_t key, VkDevice device, VkSurfaceKHR surface, std::shared_ptr<gli::texture> texture);
  void internalSetImages(uint32_t key, VkDevice device, VkSurfaceKHR surface, std::vector<std::shared_ptr<Image>>& images);
  void internalClearImage(uint32_t key, VkDevice device, VkSurfaceKHR surface, const glm::vec4& clearValue, const ImageSubresourceRange& range);
};

class PUMEX_EXPORT ImageView : public std::enable_shared_from_this<ImageView>
{
public:
  ImageView()                            = delete;
  ImageView(std::shared_ptr<MemoryImage> texture, const ImageSubresourceRange& subresourceRange, VkImageViewType viewType, VkFormat format = VK_FORMAT_UNDEFINED, const gli::swizzles& swizzles = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA));
  ImageView(const ImageView&)            = delete;
  ImageView& operator=(const ImageView&) = delete;
  ImageView(ImageView&&)                 = delete;
  ImageView& operator=(ImageView&&)      = delete;
  virtual ~ImageView();

  VkImage      getHandleImage(const RenderContext& renderContext) const;
  VkImageView  getImageView(const RenderContext& renderContext) const;

  void         validate(const RenderContext& renderContext);
  void         notify(const RenderContext& renderContext);
  void         invalidateResources();

  void         addResource(std::shared_ptr<Resource> resource);

  std::shared_ptr<MemoryImage> memoryImage;
  ImageSubresourceRange        subresourceRange;
  VkImageViewType              viewType;
  VkFormat                     format;
  gli::swizzles                swizzles;
protected:
  struct ImageViewInternal
  {
    ImageViewInternal()
      : imageView{VK_NULL_HANDLE}
    {}
    VkImageView imageView;
  };
  typedef PerObjectData<ImageViewInternal, uint32_t> ImageViewData;
  mutable std::mutex                          mutex;
  std::vector<std::weak_ptr<Resource>>        resources;
  std::unordered_map<uint32_t, ImageViewData> perObjectData;
  uint32_t                                    activeCount;
  bool                                        registered = false;

  void notifyResources(const RenderContext& renderContext);

};

const ImageTraits&                     MemoryImage::getImageTraits() const             { return imageTraits; }
VkImageAspectFlags                     MemoryImage::getAspectMask() const              { return aspectMask; }
const PerObjectBehaviour&              MemoryImage::getPerObjectBehaviour() const      { return perObjectBehaviour; }
const SwapChainImageBehaviour&         MemoryImage::getSwapChainImageBehaviour() const { return swapChainImageBehaviour; }
std::shared_ptr<DeviceMemoryAllocator> MemoryImage::getAllocator() const               { return allocator; }
std::shared_ptr<gli::texture>          MemoryImage::getTexture() const                 { return texture; }

}
