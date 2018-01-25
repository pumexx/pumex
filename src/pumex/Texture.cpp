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

namespace pumex
{

ImageTraits::ImageTraits(VkImageUsageFlags u, VkFormat f, const VkExtent3D& e, bool lt, uint32_t m, uint32_t l, VkSampleCountFlagBits s, VkImageLayout il, 
  VkImageAspectFlags am, VkMemoryPropertyFlags mp, VkImageCreateFlags ic, VkImageType it, VkSharingMode sm, VkImageViewType vt, const gli::swizzles& sw)
  : usage{ u }, linearTiling{ lt }, format{ f }, extent( e ), mipLevels{ m }, arrayLayers{ l }, samples{ s }, initialLayout{ il }, imageCreate{ ic }, imageType{ it }, sharingMode{ sm }, viewType{ vt }, swizzles{ sw }, aspectMask{ am }, memoryProperty{ mp }
{
}

SamplerTraits::SamplerTraits(VkImageUsageFlags u, bool lt, VkFilter maf, VkFilter mif, VkSamplerMipmapMode mm,  VkSamplerAddressMode au, VkSamplerAddressMode av, VkSamplerAddressMode aw, float mlb, VkBool32 ae,
  float maa, VkBool32 ce, VkCompareOp co, VkBorderColor bc, VkBool32 uc)
  : usage{ u }, linearTiling{ lt }, magFilter{ maf }, minFilter{ mif }, mipmapMode{ mm }, addressModeU{ au }, addressModeV{ av }, addressModeW{ aw }, mipLodBias{ mlb }, anisotropyEnable{ ae }, maxAnisotropy{ maa }, compareEnable{ ce }, compareOp{ co }, borderColor{ bc }, unnormalizedCoordinates{ uc }
{
}

Image::Image(Device* d, const ImageTraits& it, std::shared_ptr<DeviceMemoryAllocator> a)
  : imageTraits{ it }, device(d->device), allocator{ a }, ownsImage{ true }
{
  VkImageCreateInfo imageCI{};
    imageCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.flags         = imageTraits.imageCreate;
    imageCI.imageType     = imageTraits.imageType;
    imageCI.format        = imageTraits.format;
    imageCI.extent        = imageTraits.extent;
    imageCI.mipLevels     = imageTraits.mipLevels;
    imageCI.arrayLayers   = imageTraits.arrayLayers;
    imageCI.samples       = imageTraits.samples;
    imageCI.tiling        = imageTraits.linearTiling ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage         = imageTraits.usage;
    imageCI.sharingMode   = imageTraits.sharingMode;
//    imageCI.queueFamilyIndexCount;
//    imageCI.pQueueFamilyIndices;
    imageCI.initialLayout = imageTraits.initialLayout;
  VK_CHECK_LOG_THROW(vkCreateImage(device, &imageCI, nullptr, &image), "failed vkCreateImage");

  imageLayout = imageTraits.initialLayout;
  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(device, image, &memReqs);

  memoryBlock = allocator->allocate(d, memReqs);
  CHECK_LOG_THROW(memoryBlock.alignedSize == 0, "Cannot allocate memory for Image");
  VK_CHECK_LOG_THROW(vkBindImageMemory(device, image, memoryBlock.memory, memoryBlock.alignedOffset), "failed vkBindImageMemory");
  
  //VkMemoryAllocateInfo mem_alloc{};
  //  mem_alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  //  mem_alloc.pNext           = nullptr;
  //  mem_alloc.allocationSize  = memReqs.size;
  //  mem_alloc.memoryTypeIndex = d->physical.lock()->getMemoryType(memReqs.memoryTypeBits, imageTraits.memoryProperty);
  //VK_CHECK_LOG_THROW(vkAllocateMemory(device, &mem_alloc, nullptr, &deviceMemory), "failed vkAllocateMemory " << mem_alloc.allocationSize << " " << mem_alloc.memoryTypeIndex);
  //VK_CHECK_LOG_THROW(vkBindImageMemory(device, image, deviceMemory, 0), "failed vkBindImageMemory");

  VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.flags      = 0;
    imageViewCI.image      = image;
    imageViewCI.viewType   = imageTraits.viewType;
    imageViewCI.format     = imageTraits.format;
    imageViewCI.components = vulkanComponentMappingFromGliComponentMapping(imageTraits.swizzles);
    imageViewCI.subresourceRange.aspectMask     = imageTraits.aspectMask;
    imageViewCI.subresourceRange.baseMipLevel   = 0;
    imageViewCI.subresourceRange.levelCount     = imageTraits.mipLevels;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount     = imageTraits.arrayLayers;
  VK_CHECK_LOG_THROW(vkCreateImageView(device, &imageViewCI, nullptr, &imageView), "failed vkCreateImageView");
}

Image::Image(Device* d, VkImage i, VkFormat format, uint32_t mipLevels, uint32_t arrayLayers, VkImageAspectFlags aspectMask, VkImageViewType viewType, const gli::swizzles& swizzles)
  : device(d->device), image{ i }, ownsImage {  false }
{
  // gather all what we know about delivered image
  imageTraits.format      = format;
  imageTraits.mipLevels   = mipLevels;
  imageTraits.arrayLayers = arrayLayers;
  imageTraits.aspectMask  = aspectMask;
  imageTraits.viewType    = viewType;
  imageTraits.swizzles    = swizzles;
  imageLayout             = VK_IMAGE_LAYOUT_UNDEFINED;
//  vkGetImageMemoryRequirements(device, image, &memReqs);

  VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.flags      = 0;
    imageViewCI.image      = image;
    imageViewCI.viewType   = viewType;
    imageViewCI.format     = format;
    imageViewCI.components = vulkanComponentMappingFromGliComponentMapping(swizzles);
    imageViewCI.subresourceRange.aspectMask     = aspectMask;
    imageViewCI.subresourceRange.baseMipLevel   = 0;
    imageViewCI.subresourceRange.levelCount     = mipLevels;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount     = arrayLayers;
  VK_CHECK_LOG_THROW(vkCreateImageView(device, &imageViewCI, nullptr, &imageView), "failed vkCreateImageView");
}


Image::~Image()
{
  if (imageView != VK_NULL_HANDLE)
    vkDestroyImageView(device, imageView, nullptr);
  if (ownsImage)
  {
    if (image != VK_NULL_HANDLE)
      vkDestroyImage(device, image, nullptr);
    allocator->deallocate(device, memoryBlock);
  }
}

void Image::getImageSubresourceLayout(VkImageSubresource& subRes, VkSubresourceLayout& subResLayout) const
{
  vkGetImageSubresourceLayout(device, image, &subRes, &subResLayout);
}

void* Image::mapMemory(size_t offset, size_t range, VkMemoryMapFlags flags)
{
  void* data;
  VK_CHECK_LOG_THROW(vkMapMemory(device, memoryBlock.memory, memoryBlock.alignedSize + offset, range, flags, &data), "Cannot map memory to image");
  return data;
}

void Image::unmapMemory()
{
  vkUnmapMemory(device, memoryBlock.memory);
}

void Image::setImageLayout(VkImageLayout newLayout)
{
  imageLayout = newLayout;
}

Texture::Texture(const ImageTraits& it, const SamplerTraits& st, VkClearValue iv, std::shared_ptr<DeviceMemoryAllocator> a)
  : Resource{ Resource::OnceForAllSwapChainImages }, imageTraits{ it }, samplerTraits { st }, allocator{ a }
{
  initValue = iv;
}

Texture::Texture(const gli::texture& tex, const SamplerTraits& st, std::shared_ptr<DeviceMemoryAllocator> a)
  : Resource{ Resource::OnceForAllSwapChainImages }, samplerTraits{ st }, allocator{ a }
{
  texture = std::make_shared<gli::texture>(tex);

  auto textureExtents = texture->extent(0);
  bool memoryIsLocal = ((allocator->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  imageTraits.usage          = samplerTraits.usage;
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
  imageTraits.viewType       = vulkanViewTypeFromGliTarget(texture->target());
  imageTraits.swizzles       = texture->swizzles();
  imageTraits.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  imageTraits.memoryProperty = memoryIsLocal ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  initValue = makeColorClearValue(glm::vec4(0.0f));
}

Texture::~Texture()
{
  for (auto& pdd : perDeviceData)
  {
    if (pdd.second.sampler != VK_NULL_HANDLE)
      vkDestroySampler(pdd.first, pdd.second.sampler, nullptr);
  }
}

Image* Texture::getHandleImage(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return nullptr;
  return pddit->second.image.get();
}

VkSampler Texture::getHandleSampler(VkDevice device) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.sampler;
}

void Texture::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ renderContext.vkDevice, PerDeviceData() }).first;
  if (pddit->second.valid)
    return;

  // Create sampler
  if( pddit->second.sampler == VK_NULL_HANDLE )
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
    VK_CHECK_LOG_THROW( vkCreateSampler(renderContext.vkDevice, &sampler, nullptr, &pddit->second.sampler) , "Cannot create sampler");
  }

  if (pddit->second.image.get() == nullptr)
  {
    imageTraits.usage = imageTraits.usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    pddit->second.image = std::make_shared<Image>(renderContext.device, imageTraits, allocator);
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
      cmdBuffer->setImageLayout( *(pddit->second.image.get()), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

      // Copy mip levels from staging buffer
      cmdBuffer->cmdCopyBufferToImage(stagingBuffer->buffer, (*pddit->second.image.get()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bufferCopyRegions);

      // Change texture image layout to shader read after all mip levels have been copied
      cmdBuffer->setImageLayout( *(pddit->second.image.get()), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }
    else
    {
      // we have to copy image to host visible memory
      VkImageSubresource subRes{};
        subRes.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subRes.mipLevel   = 0;

      VkSubresourceLayout subResLayout;
      pddit->second.image->getImageSubresourceLayout(subRes, subResLayout);
      void* data = pddit->second.image->mapMemory(0, pddit->second.image->getMemorySize(), 0);
      memcpy(data, texture->data(0, 0, subRes.mipLevel), texture->size(subRes.mipLevel));
      pddit->second.image->unmapMemory();

      // Setup image memory barrier
      cmdBuffer->setImageLayout(*(pddit->second.image.get()), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    }
    renderContext.device->endSingleTimeCommands(cmdBuffer, renderContext.presentationQueue);
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
    cmdBuffer->setImageLayout(*(pddit->second.image.get()), imageTraits.aspectMask, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    if(imageTraits.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT)
      cmdBuffer->cmdClearColorImage(*(pddit->second.image.get()), VK_IMAGE_LAYOUT_GENERAL, initValue, subResources);
    else
      cmdBuffer->cmdClearDepthStencilImage(*(pddit->second.image.get()), VK_IMAGE_LAYOUT_GENERAL, initValue, subResources);
    renderContext.device->endSingleTimeCommands(cmdBuffer, renderContext.presentationQueue);

  }
  pddit->second.valid = true;
}

void Texture::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    pdd.second.valid = false;
  invalidateDescriptors();
}

void Texture::getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Texture::getDescriptorSetValue : texture was not validated");

  values.push_back( DescriptorSetValue(pddit->second.sampler, pddit->second.image->getImageView(), pddit->second.image->getImageLayout()) );
}

void Texture::setLayer(uint32_t layer, const gli::texture& tex)
{
  CHECK_LOG_THROW((layer < texture->base_layer()) || (layer >= texture->base_layer() + texture->layers()), "Layer out of bounds : " << layer << " should be between " << texture->base_layer() << " and " << texture->base_layer() + texture->layers() -1);
  CHECK_LOG_THROW(texture->format() != tex.format(), "Input texture has wrong format : " << tex.format() << " should be " << texture->format());
  CHECK_LOG_THROW((texture->extent().x != tex.extent().x) || (texture->extent().y != tex.extent().y), "Texture has wrong size : ( " << tex.extent().x << " x " << tex.extent().y << " ) should be ( " << texture->extent().x << " x " << texture->extent().y << " )");
  // FIXME - later we may add size and format conversions if needed

  for (uint32_t level = texture->base_level(); level < texture->levels(); ++level)
    memcpy(texture->data(layer, 0, level), tex.data(0, 0, level), tex.size(level));
}

VkFormat vulkanFormatFromGliFormat(gli::texture::format_type format)
{
  // Formats are almost identical. Looks like someone implemented GLI and Vulkan at the same time
  return (VkFormat)format;
}

VkImageViewType vulkanViewTypeFromGliTarget(gli::texture::target_type target)
{

  switch (target)
  {
  case gli::texture::target_type::TARGET_1D:
    return VK_IMAGE_VIEW_TYPE_1D;
  case gli::texture::target_type::TARGET_1D_ARRAY:
    return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
  case gli::texture::target_type::TARGET_2D:
    return VK_IMAGE_VIEW_TYPE_2D;
  case gli::texture::target_type::TARGET_2D_ARRAY:
    return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  case gli::texture::target_type::TARGET_3D:
    return VK_IMAGE_VIEW_TYPE_3D;
  case gli::texture::target_type::TARGET_RECT:
    return VK_IMAGE_VIEW_TYPE_2D;
  case gli::texture::target_type::TARGET_RECT_ARRAY:
    return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  case gli::texture::target_type::TARGET_CUBE:
    return VK_IMAGE_VIEW_TYPE_CUBE;
  case gli::texture::target_type::TARGET_CUBE_ARRAY:
    return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
  }
  return VK_IMAGE_VIEW_TYPE_2D;
}

VkImageType vulkanImageTypeFromTextureExtents(const gli::extent3d& extents)
{
  if (extents.z > 1)
    return VK_IMAGE_TYPE_3D;
  if (extents.y > 1)
    return VK_IMAGE_TYPE_2D;
  return VK_IMAGE_TYPE_1D;
}

VkComponentSwizzle vulkanSwizzlesFromGliSwizzles(const gli::swizzle& s)
{
  // VK_COMPONENT_SWIZZLE_IDENTITY is not represented in GLI
  switch (s)
  {
  case gli::swizzle::SWIZZLE_RED:
    return VK_COMPONENT_SWIZZLE_R;
  case gli::swizzle::SWIZZLE_GREEN:
    return VK_COMPONENT_SWIZZLE_G;
  case gli::swizzle::SWIZZLE_BLUE:
    return VK_COMPONENT_SWIZZLE_B;
  case gli::swizzle::SWIZZLE_ALPHA:
    return VK_COMPONENT_SWIZZLE_A;
  case gli::swizzle::SWIZZLE_ZERO:
    return VK_COMPONENT_SWIZZLE_ZERO;
  case gli::swizzle::SWIZZLE_ONE:
    return VK_COMPONENT_SWIZZLE_ONE;
  }
  return VK_COMPONENT_SWIZZLE_IDENTITY;
}

VkComponentMapping vulkanComponentMappingFromGliComponentMapping(const gli::swizzles& swz)
{
  VkComponentMapping mapping;
  mapping.r = vulkanSwizzlesFromGliSwizzles(swz.r);
  mapping.g = vulkanSwizzlesFromGliSwizzles(swz.g);
  mapping.b = vulkanSwizzlesFromGliSwizzles(swz.b);
  mapping.a = vulkanSwizzlesFromGliSwizzles(swz.a);
  return mapping;
}


}