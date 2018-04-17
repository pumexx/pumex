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
#include <pumex/PhysicalDevice.h>
#include <pumex/RenderContext.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

using namespace pumex;

Texture::Texture(const ImageTraits& it, std::shared_ptr<DeviceMemoryAllocator> a, VkClearValue iv, Resource::SwapChainImageBehaviour scib)
  : Resource{ scib }, imageTraits{ it }, useSampler{ false }, samplerTraits(), allocator{ a }
{
  initValue = iv;
}

Texture::Texture(const ImageTraits& it, const SamplerTraits& st, std::shared_ptr<DeviceMemoryAllocator> a, VkClearValue iv, Resource::SwapChainImageBehaviour scib)
  : Resource{ scib }, imageTraits{ it }, useSampler{ true }, samplerTraits { st }, allocator{ a }
{
  initValue = iv;
}

Texture::Texture(std::shared_ptr<gli::texture> tex, std::shared_ptr<DeviceMemoryAllocator> a, VkImageUsageFlags usage, Resource::SwapChainImageBehaviour scib)
  : Resource{ scib }, useSampler{ false }, samplerTraits(), texture{ tex }, allocator{ a }
{
  buildImageTraits(usage);
}


Texture::Texture(std::shared_ptr<gli::texture> tex, const SamplerTraits& st, std::shared_ptr<DeviceMemoryAllocator> a, VkImageUsageFlags usage, Resource::SwapChainImageBehaviour scib)
  : Resource{ scib }, useSampler{ true }, samplerTraits{ st }, texture{ tex }, allocator { a }
{
  buildImageTraits(usage);
}

void Texture::buildImageTraits(VkImageUsageFlags usage)
{
  auto textureExtents = texture->extent(0);
  bool memoryIsLocal = ((allocator->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  imageTraits.usage = usage;
  imageTraits.linearTiling = false;
  imageTraits.format = vulkanFormatFromGliFormat(texture->format());
  imageTraits.extent = { uint32_t(textureExtents.x), uint32_t(textureExtents.y), uint32_t(textureExtents.z) };
  imageTraits.mipLevels = texture->levels();
  imageTraits.arrayLayers = texture->layers();
  imageTraits.samples = VK_SAMPLE_COUNT_1_BIT;
  imageTraits.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageTraits.imageCreate = 0;
  imageTraits.imageType = vulkanImageTypeFromTextureExtents(textureExtents);
  imageTraits.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageTraits.viewType = vulkanViewTypeFromGliTarget(texture->target());
  imageTraits.swizzles = texture->swizzles();
  imageTraits.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageTraits.memoryProperty = memoryIsLocal ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

Texture::~Texture()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
  {
    for (uint32_t i = 0; i < pdd.second.image.size(); ++i)
    {
      pdd.second.image[i] = nullptr;
      if (pdd.second.sampler[i] != VK_NULL_HANDLE)
        vkDestroySampler(pdd.first, pdd.second.sampler[i], nullptr);
    }
  }
}

Image* Texture::getHandleImage(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == perDeviceData.end())
    return nullptr;
  return pddit->second.image[renderContext.activeIndex % activeCount].get();
}

VkSampler Texture::getHandleSampler(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.sampler[renderContext.activeIndex % activeCount];
}

void Texture::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (swapChainImageBehaviour == Resource::ForEachSwapChainImage && renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perDeviceData)
      pdd.second.resize(activeCount);
  }
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ renderContext.vkDevice, PerDeviceData(activeCount) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  // Create sampler
  if( useSampler && pddit->second.sampler[activeIndex] == VK_NULL_HANDLE )
  {
    VkSamplerCreateInfo sampler{};
      sampler.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      sampler.magFilter               = samplerTraits.magFilter;
      sampler.minFilter               = samplerTraits.minFilter;
      sampler.mipmapMode              = samplerTraits.mipmapMode;
      sampler.addressModeU            = samplerTraits.addressModeU;
      sampler.addressModeV            = samplerTraits.addressModeV;
      sampler.addressModeW            = samplerTraits.addressModeW;
      sampler.mipLodBias              = samplerTraits.mipLodBias;
      sampler.anisotropyEnable        = samplerTraits.anisotropyEnable;
      sampler.maxAnisotropy           = samplerTraits.maxAnisotropy;
      sampler.compareEnable           = samplerTraits.compareEnable;
      sampler.compareOp               = samplerTraits.compareOp;
      sampler.minLod                  = 0.0f;
      sampler.maxLod                  = (!samplerTraits.linearTiling) ? (float)texture->levels() : 0.0f;
      sampler.borderColor             = samplerTraits.borderColor;
      sampler.unnormalizedCoordinates = samplerTraits.unnormalizedCoordinates;
    VK_CHECK_LOG_THROW( vkCreateSampler(renderContext.vkDevice, &sampler, nullptr, &pddit->second.sampler[activeIndex]) , "Cannot create sampler");
  }

  if (pddit->second.image[activeIndex] == nullptr)
  {
    imageTraits.usage = imageTraits.usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    pddit->second.image[activeIndex] = std::make_shared<Image>(renderContext.device, imageTraits, allocator);
  }

  bool memoryIsLocal = ( imageTraits.memoryProperty == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
  CHECK_LOG_THROW(memoryIsLocal && samplerTraits.linearTiling, "Cannot have texture with linear tiling in device local memory");

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
      cmdBuffer->setImageLayout( *(pddit->second.image[activeIndex].get()), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

      // Copy mip levels from staging buffer
      cmdBuffer->cmdCopyBufferToImage(stagingBuffer->buffer, (*pddit->second.image[activeIndex].get()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bufferCopyRegions);

      // Change texture image layout to shader read after all mip levels have been copied
      cmdBuffer->setImageLayout( *(pddit->second.image[activeIndex].get()), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }
    else
    {
      // we have to copy image to host visible memory
      VkImageSubresource subRes{};
        subRes.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subRes.mipLevel   = 0;

      VkSubresourceLayout subResLayout;
      pddit->second.image[activeIndex]->getImageSubresourceLayout(subRes, subResLayout);
      void* data = pddit->second.image[activeIndex]->mapMemory(0, pddit->second.image[activeIndex]->getMemorySize(), 0);
      memcpy(data, texture->data(0, 0, subRes.mipLevel), texture->size(subRes.mipLevel));
      pddit->second.image[activeIndex]->unmapMemory();

      // Setup image memory barrier
      cmdBuffer->setImageLayout(*(pddit->second.image[activeIndex].get()), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    }
    renderContext.device->endSingleTimeCommands(cmdBuffer, renderContext.queue);
    renderContext.device->releaseStagingBuffer(stagingBuffer);
  }
  else
  {
    // we have to clear the data with predefined value
    VkImageSubresourceRange subRes{};
    subRes.aspectMask     = imageTraits.aspectMask;
    subRes.baseMipLevel   = 0;
    subRes.levelCount     = imageTraits.mipLevels;
    subRes.baseArrayLayer = 0;
    subRes.layerCount     = imageTraits.arrayLayers;
    std::vector<VkImageSubresourceRange> subResources;
    subResources.push_back(subRes);

    auto cmdBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
    cmdBuffer->setImageLayout(*(pddit->second.image[activeIndex].get()), imageTraits.aspectMask, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    if(imageTraits.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT)
      cmdBuffer->cmdClearColorImage(*(pddit->second.image[activeIndex].get()), VK_IMAGE_LAYOUT_GENERAL, initValue, subResources);
    else
      cmdBuffer->cmdClearDepthStencilImage(*(pddit->second.image[activeIndex].get()), VK_IMAGE_LAYOUT_GENERAL, initValue, subResources);
    renderContext.device->endSingleTimeCommands(cmdBuffer, renderContext.queue);

  }
  pddit->second.valid[activeIndex] = true;
}

void Texture::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    for (uint32_t i = 0; i<pdd.second.valid.size(); ++i)
      pdd.second.valid[i] = false;
  invalidateDescriptors();
}

DescriptorSetValue Texture::getDescriptorSetValue(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Texture::getDescriptorSetValue() : texture was not validated");
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  return DescriptorSetValue(pddit->second.sampler[activeIndex], pddit->second.image[activeIndex]->getImageView(), pddit->second.image[activeIndex]->getImageLayout());
}

void Texture::setLayer(uint32_t layer, std::shared_ptr<gli::texture> tex)
{
  CHECK_LOG_THROW((layer < texture->base_layer()) || (layer >= texture->base_layer() + texture->layers()), "Layer out of bounds : " << layer << " should be between " << texture->base_layer() << " and " << texture->base_layer() + texture->layers() -1);
  CHECK_LOG_THROW(texture->format() != tex->format(), "Input texture has wrong format : " << tex->format() << " should be " << texture->format());
  CHECK_LOG_THROW((texture->extent().x != tex->extent().x) || (texture->extent().y != tex->extent().y), "Texture has wrong size : ( " << tex->extent().x << " x " << tex->extent().y << " ) should be ( " << texture->extent().x << " x " << texture->extent().y << " )");
  // FIXME - later we may add size and format conversions if needed

  for (uint32_t level = texture->base_level(); level < texture->levels(); ++level)
    memcpy(texture->data(layer, 0, level), tex->data(0, 0, level), tex->size(level));
}
