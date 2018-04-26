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

#include <pumex/Texture.h>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/RenderContext.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

using namespace pumex;

ImageSubresourceRange::ImageSubresourceRange(VkImageAspectFlags am, uint32_t m0, uint32_t mc, uint32_t a0, uint32_t ac)
  : aspectMask{ am }, baseMipLevel{ m0 }, levelCount{ mc }, baseArrayLayer{ a0 }, layerCount{ ac }
{
}

VkImageSubresourceRange ImageSubresourceRange::getSubresource()
{
  VkImageSubresourceRange result;
    result.aspectMask     = aspectMask;
    result.baseMipLevel   = baseMipLevel;
    result.levelCount     = levelCount;
    result.baseArrayLayer = baseArrayLayer;
    result.layerCount     = layerCount;
  return result;
}

Texture::Texture(const ImageTraits& it, std::shared_ptr<DeviceMemoryAllocator> a, const glm::vec4& iv, PerObjectBehaviour pob, SwapChainImageBehaviour scib)
  : perObjectBehaviour{ pob }, swapChainImageBehaviour{ scib }, imageTraits{ it }, allocator{ a }, activeCount{ 1 }
{
//  if (imageTraits.aspectMask | VK_IMAGE_ASPECT_COLOR_BIT)
    initValue = makeColorClearValue(iv);
//  else
//    initValue = makeDepthStencilClearValue(iv.x, iv.y);
}

Texture::Texture(std::shared_ptr<gli::texture> tex, std::shared_ptr<DeviceMemoryAllocator> a, VkImageUsageFlags usage, PerObjectBehaviour pob, SwapChainImageBehaviour scib)
  : perObjectBehaviour{ pob }, swapChainImageBehaviour{ scib }, texture{ tex }, allocator { a }, activeCount{ 1 }
{
  buildImageTraits(usage);
  initValue = makeColorClearValue(glm::vec4(0.0f));
}

Texture::~Texture()
{
  std::lock_guard<std::mutex> lock(mutex);
  perObjectData.clear();
}

void Texture::buildImageTraits(VkImageUsageFlags usage)
{
  auto textureExtents = texture->extent(0);
  bool memoryIsLocal = ((allocator->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  imageTraits.usage          = usage;
  imageTraits.linearTiling   = false;
  imageTraits.format         = vulkanFormatFromGliFormat(texture->format());
  imageTraits.extent         = { uint32_t(textureExtents.x), uint32_t(textureExtents.y), uint32_t(textureExtents.z) };
  imageTraits.mipLevels      = texture->levels();
  imageTraits.arrayLayers    = texture->layers();
  imageTraits.samples        = VK_SAMPLE_COUNT_1_BIT;
  imageTraits.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  imageTraits.imageCreate    = 0;
  imageTraits.imageType      = vulkanImageTypeFromTextureExtents(textureExtents);
  imageTraits.sharingMode    = VK_SHARING_MODE_EXCLUSIVE;
//  imageTraits.viewType       = vulkanViewTypeFromGliTarget(texture->target());
//  imageTraits.swizzles       = texture->swizzles();
//  imageTraits.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  imageTraits.memoryProperty = memoryIsLocal ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

Image* Texture::getImage(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(getKey(renderContext, perObjectBehaviour));
  if (pddit == end(perObjectData))
    return nullptr;
  return pddit->second.data[renderContext.activeIndex % activeCount].image.get();
}

void Texture::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (swapChainImageBehaviour == swForEachImage && renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKey(renderContext, perObjectBehaviour);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, PerObjectData<TextureInternal>(renderContext) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  if (pddit->second.data[activeIndex].image == nullptr)
  {
    imageTraits.usage = imageTraits.usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    pddit->second.data[activeIndex].image = std::make_shared<Image>(renderContext.device, imageTraits, allocator);
  }

  bool memoryIsLocal = ( imageTraits.memoryProperty == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
//  CHECK_LOG_THROW(memoryIsLocal && ( sampler!=nullptr && sampler->getSamplerTraits().linearTiling ), "Cannot have texture with linear tiling in device local memory");

  if( texture.get() != nullptr )
  {
    auto cmdBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
    std::shared_ptr<StagingBuffer> stagingBuffer = renderContext.device->acquireStagingBuffer(texture->data(), texture->size());
    if (memoryIsLocal)
    {
      // we have to copy a texture to local device memory using staging buffers
      std::vector<VkBufferImageCopy> bufferCopyRegions;
      size_t offset = 0;
      for (uint32_t layer = texture->base_layer(); layer < texture->layers(); ++layer)
      {
        for (uint32_t level = texture->base_level(); level < texture->levels(); ++level)
        {
          auto mipMapExtents = texture->extent(level);
          VkBufferImageCopy bufferCopyRegion{};
            bufferCopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            bufferCopyRegion.imageSubresource.mipLevel       = level;
            bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
            bufferCopyRegion.imageSubresource.layerCount     = 1;
            bufferCopyRegion.imageExtent.width               = static_cast<uint32_t>(mipMapExtents.x);
            bufferCopyRegion.imageExtent.height              = static_cast<uint32_t>(mipMapExtents.y);
            bufferCopyRegion.imageExtent.depth               = static_cast<uint32_t>(mipMapExtents.z);
            bufferCopyRegion.bufferOffset                    = offset;
          bufferCopyRegions.push_back(bufferCopyRegion);

          // Increase offset into staging buffer for next level / face
          offset += texture->size(level);
        }
      }
      // Image barrier for optimal image (target)
      // Optimal image will be used as destination for the copy
      cmdBuffer->setImageLayout( *(pddit->second.data[activeIndex].image.get()), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

      // Copy mip levels from staging buffer
      cmdBuffer->cmdCopyBufferToImage(stagingBuffer->buffer, (*pddit->second.data[activeIndex].image.get()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bufferCopyRegions);

      // Change texture image layout to shader read after all mip levels have been copied
      cmdBuffer->setImageLayout( *(pddit->second.data[activeIndex].image.get()), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }
    else
    {
      // we have to copy image to host visible memory
      VkImageSubresource subRes{};
        subRes.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subRes.mipLevel   = 0;

      VkSubresourceLayout subResLayout;
      pddit->second.data[activeIndex].image->getImageSubresourceLayout(subRes, subResLayout);
      void* data = pddit->second.data[activeIndex].image->mapMemory(0, pddit->second.data[activeIndex].image->getMemorySize(), 0);
      memcpy(data, texture->data(0, 0, subRes.mipLevel), texture->size(subRes.mipLevel));
      pddit->second.data[activeIndex].image->unmapMemory();

      // Setup image memory barrier
      cmdBuffer->setImageLayout(*(pddit->second.data[activeIndex].image.get()), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    }
    renderContext.device->endSingleTimeCommands(cmdBuffer, renderContext.queue);
    renderContext.device->releaseStagingBuffer(stagingBuffer);
  }
  else
  {
    // we have to clear the data with predefined value
    VkImageSubresourceRange subRes{};
    subRes.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;// imageTraits.aspectMask;
    subRes.baseMipLevel   = 0;
    subRes.levelCount     = imageTraits.mipLevels;
    subRes.baseArrayLayer = 0;
    subRes.layerCount     = imageTraits.arrayLayers;
    std::vector<VkImageSubresourceRange> subResources;
    subResources.push_back(subRes);

    auto cmdBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
    cmdBuffer->setImageLayout(*(pddit->second.data[activeIndex].image.get()), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    cmdBuffer->cmdClearColorImage(*(pddit->second.data[activeIndex].image.get()), VK_IMAGE_LAYOUT_GENERAL, initValue, subResources);
    renderContext.device->endSingleTimeCommands(cmdBuffer, renderContext.queue);

  }
  pddit->second.valid[activeIndex] = true;
}

void Texture::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
    std::fill(begin(pdd.second.valid), end(pdd.second.valid), false);
  invalidateImageViews();
}

void Texture::setLayer(uint32_t layer, std::shared_ptr<gli::texture> tex)
{
  CHECK_LOG_THROW((layer < texture->base_layer()) || (layer >= texture->base_layer() + texture->layers()), "Layer out of bounds : " << layer << " should be between " << texture->base_layer() << " and " << texture->base_layer() + texture->layers() -1);
  CHECK_LOG_THROW(texture->format() != tex->format(), "Input texture has wrong format : " << tex->format() << " should be " << texture->format());
  CHECK_LOG_THROW((texture->extent().x != tex->extent().x) || (texture->extent().y != tex->extent().y), "Texture has wrong size : ( " << tex->extent().x << " x " << tex->extent().y << " ) should be ( " << texture->extent().x << " x " << texture->extent().y << " )");
  // FIXME - later we may add size and format conversions if needed

  for (uint32_t level = texture->base_level(); level < texture->levels(); ++level)
    memcpy(texture->data(layer, 0, level), tex->data(0, 0, level), tex->size(level));
  invalidate();
}

ImageSubresourceRange Texture::getFullImageRange()
{
  if (texture != nullptr)
    return ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, texture->base_level(), texture->layers(), texture->base_layer(), texture->layers());
  return ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, imageTraits.mipLevels, 0, imageTraits.arrayLayers);
}

void Texture::addImageView(std::shared_ptr<ImageView> imageView)
{
  if (std::find_if(begin(imageViews), end(imageViews), [&imageView](std::weak_ptr<ImageView> ia) { return !ia.expired() && ia.lock().get() == imageView.get(); }) == end(imageViews))
    imageViews.push_back(imageView);
}

void Texture::invalidateImageViews()
{
  auto eit = std::remove_if(begin(imageViews), end(imageViews), [](std::weak_ptr<ImageView> ia) { return ia.expired();  });
  for (auto it = begin(imageViews); it != eit; ++it)
    it->lock()->invalidate();
  imageViews.erase(eit, end(imageViews));
}

ImageView::ImageView(std::shared_ptr<Texture> t, const ImageSubresourceRange& sr, VkImageViewType vt, VkFormat f, const gli::swizzles& sw)
  : texture{ t }, subresourceRange{ sr }, viewType{ vt }, swizzles{ sw }, activeCount{ 1 }
{
  format = (f == VK_FORMAT_UNDEFINED) ? texture->getImageTraits().format : f;
  texture->addImageView(shared_from_this());
}

ImageView::~ImageView()
{
  std::lock_guard<std::mutex> lock(mutex);
  for( auto& pdd : perObjectData )
    for( uint32_t i=0; i<pdd.second.data.size(); ++i)
      vkDestroyImageView(pdd.second.device, pdd.second.data[i].imageView, nullptr);
}

VkImage ImageView::getHandleImage(const RenderContext& renderContext) const
{
  return texture->getImage(renderContext)->getHandleImage();
}

VkImageView ImageView::getImageView(const RenderContext& renderContext) const
{
  auto keyValue = getKey(renderContext, texture->getPerObjectBehaviour());
  auto pddit = perObjectData.find(keyValue);
  if (pddit == perObjectData.end())
    return VK_NULL_HANDLE;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  return pddit->second.data[activeIndex].imageView;
}

void ImageView::validate(const RenderContext& renderContext)
{
  texture->validate(renderContext);
  std::lock_guard<std::mutex> lock(mutex);
  if (texture->getSwapChainImageBehaviour() == swForEachImage && renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKey(renderContext, texture->getPerObjectBehaviour());
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, PerObjectData<ImageViewInternal>(renderContext) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  if (pddit->second.data[activeIndex].imageView != VK_NULL_HANDLE)
  {
    vkDestroyImageView(pddit->second.device, pddit->second.data[activeIndex].imageView, nullptr);
    pddit->second.data[activeIndex].imageView = VK_NULL_HANDLE;
  }

  VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.flags            = 0;
    imageViewCI.image            = getHandleImage(renderContext);
    imageViewCI.viewType         = viewType;
    imageViewCI.format           = format;
    imageViewCI.components       = vulkanComponentMappingFromGliComponentMapping(swizzles);
    imageViewCI.subresourceRange = subresourceRange.getSubresource();
  VK_CHECK_LOG_THROW(vkCreateImageView(pddit->second.device, &imageViewCI, nullptr, &pddit->second.data[activeIndex].imageView), "failed vkCreateImageView");
  
  pddit->second.valid[activeIndex] = true;
}

void ImageView::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
    std::fill(begin(pdd.second.valid), end(pdd.second.valid), false);
}

void ImageView::addResource(std::shared_ptr<Resource> resource)
{
  if (std::find_if(begin(resources), end(resources), [&resource](std::weak_ptr<Resource> ia) { return !ia.expired() && ia.lock().get() == resource.get(); }) == end(resources))
    resources.push_back(resource);
}

void ImageView::invalidateResources()
{
  auto eit = std::remove_if(begin(resources), end(resources), [](std::weak_ptr<Resource> r) { return r.expired();  });
  for (auto it = begin(resources); it != eit; ++it)
    it->lock()->invalidate();
  resources.erase(eit, end(resources));
}
