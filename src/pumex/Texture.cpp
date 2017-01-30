#include <pumex/Texture.h>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

using namespace pumex;

Texture::Texture()
{
}

Texture::Texture(const gli::texture& tex, const TextureTraits& tr)
  : traits{ tr }
{
  texture = std::make_shared<gli::texture>(tex);
}


Texture::~Texture()
{
  for (auto& pdd : perDeviceData)
  {
    if (pdd.second.imageView != VK_NULL_HANDLE)
      vkDestroyImageView(pdd.first, pdd.second.imageView, nullptr);
    if (pdd.second.sampler != VK_NULL_HANDLE)
      vkDestroySampler(pdd.first, pdd.second.sampler, nullptr);
    if (pdd.second.image != VK_NULL_HANDLE)
      vkDestroyImage(pdd.first, pdd.second.image, nullptr);
    if (pdd.second.deviceMemory != VK_NULL_HANDLE)
      vkFreeMemory(pdd.first, pdd.second.deviceMemory, nullptr);
  }
}

void Texture::setDirty()
{
  for (auto& pdd : perDeviceData)
    pdd.second.dirty = true;
}

VkImage Texture::getHandle(VkDevice device) const
{
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return VK_NULL_HANDLE;
  return pddit->second.image;
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
  return VK_COMPONENT_SWIZZLE_R;
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

void Texture::validate(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandPool> commandPool, VkQueue queue)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (!pddit->second.dirty)
    return;

  if (pddit->second.image == VK_NULL_HANDLE)
  {
  }
  VkFormat format = vulkanFormatFromGliFormat(texture->format());

  VkFormatProperties formatProperties;
  vkGetPhysicalDeviceFormatProperties(device->physical.lock()->physicalDevice, format, &formatProperties);

  // Only use linear tiling if requested (and supported by the device)
  // Support for linear tiling is mostly limited, so prefer to use
  // optimal tiling instead
  // On most implementations linear tiling will only support a very
  // limited amount of formats and features (mip maps, cubemaps, arrays, etc.)
  VkBool32 useStaging = ! traits.linearTiling;

  auto cmdBuffer = device->beginSingleTimeCommands(commandPool);
  if (useStaging)
  {
    // Create a host-visible staging buffer that contains the raw image data
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    // bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkDeviceSize stagingSize = createBuffer(device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, texture->size(), &stagingBuffer, &stagingMemory, texture->data());
    CHECK_LOG_THROW(stagingSize == 0, "Cannot create staging buffer");

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
          bufferCopyRegion.imageExtent.depth               = 1;
          bufferCopyRegion.bufferOffset                    = offset;
        bufferCopyRegions.push_back(bufferCopyRegion);

        // Increase offset into staging buffer for next level / face
        offset += texture->size(level);
      }
    }

    auto textureExtents = texture->extent(0);
    // Create optimal tiled target image
    VkImageCreateInfo imageCreateInfo{};
      imageCreateInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      imageCreateInfo.imageType     = VK_IMAGE_TYPE_2D;
      imageCreateInfo.format        = format;
      imageCreateInfo.mipLevels     = texture->levels();
      imageCreateInfo.arrayLayers   = texture->layers();
      imageCreateInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
      imageCreateInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
      imageCreateInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
      imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      imageCreateInfo.extent        = { uint32_t(textureExtents.x), uint32_t(textureExtents.y), 1 };
      imageCreateInfo.usage         = traits.imageUsageFlags;
    // Ensure that the TRANSFER_DST bit is set for staging
    if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    {
      imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    VK_CHECK_LOG_THROW(vkCreateImage(device->device, &imageCreateInfo, nullptr, &pddit->second.image), "Cannot create an image");

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device->device, pddit->second.image, &memReqs);

    VkMemoryAllocateInfo memAllocInfo{};
      memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      memAllocInfo.allocationSize = memReqs.size;
      memAllocInfo.memoryTypeIndex = device->physical.lock()->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_LOG_THROW( vkAllocateMemory(device->device, &memAllocInfo, nullptr, &pddit->second.deviceMemory), "Cannot allocate memory for image");
    VK_CHECK_LOG_THROW(vkBindImageMemory(device->device, pddit->second.image, pddit->second.deviceMemory, 0), "Cannot bind memory to image");

    VkImageSubresourceRange subresourceRange{};
      subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      subresourceRange.baseMipLevel   = texture->base_level();
      subresourceRange.levelCount     = texture->levels();
      subresourceRange.baseArrayLayer = texture->base_layer();
      subresourceRange.layerCount     = texture->layers();

    // Image barrier for optimal image (target)
    // Optimal image will be used as destination for the copy
    setImageLayout( cmdBuffer->getHandle(device->device), pddit->second.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

    // Copy mip levels from staging buffer
    vkCmdCopyBufferToImage(cmdBuffer->getHandle(device->device), stagingBuffer, pddit->second.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

    // Change texture image layout to shader read after all mip levels have been copied
    pddit->second.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    setImageLayout(cmdBuffer->getHandle(device->device), pddit->second.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, pddit->second.imageLayout, subresourceRange);

    device->endSingleTimeCommands(cmdBuffer, queue);

    // Clean up staging resources
    pumex::destroyBuffer(device, stagingBuffer, stagingMemory);
  }
  else
  {
    // Prefer using optimal tiling, as linear tiling 
    // may support only a small set of features 
    // depending on implementation (e.g. no mip maps, only one layer, etc.)

    // Check if this support is supported for linear tiling
    assert(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    auto textureExtents = texture->extent(0);

    VkImageCreateInfo imageCreateInfo{};
      imageCreateInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      imageCreateInfo.imageType     = VK_IMAGE_TYPE_2D;
      imageCreateInfo.format        = format;
      imageCreateInfo.extent        = { uint32_t(textureExtents.x), uint32_t(textureExtents.y), 1 };
      imageCreateInfo.mipLevels     = 1;
      imageCreateInfo.arrayLayers   = texture->layers();
      imageCreateInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
      imageCreateInfo.tiling        = VK_IMAGE_TILING_LINEAR;
      imageCreateInfo.usage         = traits.imageUsageFlags;
      imageCreateInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
      imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    // Load mip map level 0 to linear tiling image
    VK_CHECK_LOG_THROW( vkCreateImage(device->device, &imageCreateInfo, nullptr, &pddit->second.image), "Cannot create image");

    // Get memory requirements for this image 
    // like size and alignment
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device->device, pddit->second.image, &memReqs);
    // Set memory allocation size to required memory size
    VkMemoryAllocateInfo memAllocInfo{};
      memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      memAllocInfo.allocationSize = memReqs.size;
      memAllocInfo.memoryTypeIndex = device->physical.lock()->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    // Allocate host memory
    VK_CHECK_LOG_THROW(vkAllocateMemory(device->device, &memAllocInfo, nullptr, &pddit->second.deviceMemory), "Cannot allocate memory for image");

    // Bind allocated image for use
    VK_CHECK_LOG_THROW( vkBindImageMemory( device->device, pddit->second.image, pddit->second.deviceMemory, 0), "Cannot bind memory to image");

    // Get sub resource layout
    // Mip map count, array layer, etc.
    VkImageSubresource subRes{};
      subRes.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      subRes.mipLevel   = 0;

    VkSubresourceLayout subResLayout;
    void *data;

    // Get sub resources layout 
    // Includes row pitch, size offsets, etc.
    vkGetImageSubresourceLayout(device->device, pddit->second.image, &subRes, &subResLayout);

    // Map image memory
    VK_CHECK_LOG_THROW( vkMapMemory(device->device, pddit->second.deviceMemory, 0, memReqs.size, 0, &data), "Cannot map memory to image" );

    // Copy image data into memory
    
    memcpy(data, texture->data(0, 0, subRes.mipLevel), texture->size(subRes.mipLevel));

    vkUnmapMemory(device->device, pddit->second.deviceMemory);

    // Linear tiled images don't need to be staged
    // and can be directly used as textures
    pddit->second.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Setup image memory barrier
    setImageLayout(cmdBuffer->getHandle(device->device), pddit->second.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, pddit->second.imageLayout);

    device->endSingleTimeCommands(cmdBuffer, queue);
  }

  // Create sampler
  VkSamplerCreateInfo sampler{};
    sampler.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter               = traits.magFilter;
    sampler.minFilter               = traits.minFilter;
    sampler.mipmapMode              = traits.mipmapMode;
    sampler.addressModeU            = traits.addressModeU;
    sampler.addressModeV            = traits.addressModeV;
    sampler.addressModeW            = traits.addressModeW;
    sampler.mipLodBias              = traits.mipLodBias;
    sampler.anisotropyEnable        = traits.anisotropyEnable;
    sampler.maxAnisotropy           = traits.maxAnisotropy;
    sampler.compareEnable           = traits.compareEnable;
    sampler.compareOp               = traits.compareOp;
    sampler.minLod                  = 0.0f;
    sampler.maxLod                  = (useStaging) ? (float)texture->levels() : 0.0f;
    sampler.borderColor             = traits.borderColor;
    sampler.unnormalizedCoordinates = traits.unnormalizedCoordinates;
  VK_CHECK_LOG_THROW( vkCreateSampler(device->device, &sampler, nullptr, &pddit->second.sampler) , "Cannot create sampler");

  // Create image view containing the whole image
  VkImageViewCreateInfo view{};
    view.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image                           = pddit->second.image;
    view.viewType                        = vulkanViewTypeFromGliTarget(texture->target());
    view.format                          = format;
    view.components                      = vulkanComponentMappingFromGliComponentMapping(texture->swizzles());
    view.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.baseArrayLayer = texture->base_layer();
    view.subresourceRange.layerCount     = texture->layers();
    view.subresourceRange.baseMipLevel   = texture->base_level();
    view.subresourceRange.levelCount     = (useStaging) ? texture->levels() : 1;
  VK_CHECK_LOG_THROW( vkCreateImageView(device->device, &view, nullptr, &pddit->second.imageView), "Cannot create image view");

  //pddit->second.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // FIXME : imageLayout

  pddit->second.dirty = false;
}


void pumex::setImageLayout( VkCommandBuffer cmdbuffer, VkImage image, VkImageAspectFlags aspectMask, 
  VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkImageSubresourceRange subresourceRange)
{
  // Create an image barrier object
  VkImageMemoryBarrier imageMemoryBarrier{};
    imageMemoryBarrier.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.oldLayout        = oldImageLayout;
    imageMemoryBarrier.newLayout        = newImageLayout;
    imageMemoryBarrier.image            = image;
    imageMemoryBarrier.subresourceRange = subresourceRange;

  // Source layouts (old)
  // Source access mask controls actions that have to be finished on the old layout
  // before it will be transitioned to the new layout
  switch (oldImageLayout)
  {
  case VK_IMAGE_LAYOUT_UNDEFINED:
    // Image layout is undefined (or does not matter)
    // Only valid as initial layout
    // No flags required, listed only for completeness
    imageMemoryBarrier.srcAccessMask = 0;
    break;
  case VK_IMAGE_LAYOUT_PREINITIALIZED:
    // Image is preinitialized
    // Only valid as initial layout for linear images, preserves memory contents
    // Make sure host writes have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    // Image is a color attachment
    // Make sure any writes to the color buffer have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    // Image is a depth/stencil attachment
    // Make sure any writes to the depth/stencil buffer have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    // Image is a transfer source 
    // Make sure any reads from the image have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    // Image is a transfer destination
    // Make sure any writes to the image have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    // Image is read by a shader
    // Make sure any shader reads from the image have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    break;
  }

  // Target layouts (new)
  // Destination access mask controls the dependency for the new image layout
  switch (newImageLayout)
  {
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    // Image will be used as a transfer destination
    // Make sure any writes to the image have been finished
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    // Image will be used as a transfer source
    // Make sure any reads from and writes to the image have been finished
    imageMemoryBarrier.srcAccessMask = imageMemoryBarrier.srcAccessMask | VK_ACCESS_TRANSFER_READ_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    break;
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    // Image will be used as a color attachment
    // Make sure any writes to the color buffer have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    // Image layout will be used as a depth/stencil attachment
    // Make sure any writes to depth/stencil buffer have been finished
    imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    // Image will be read in a shader (sampler, input attachment)
    // Make sure any writes to the image have been finished
    if (imageMemoryBarrier.srcAccessMask == 0)
    {
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    break;
  }

  // Put barrier on top
  VkPipelineStageFlags srcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags destStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

  // Put barrier inside setup command buffer
  vkCmdPipelineBarrier( cmdbuffer, srcStageFlags, destStageFlags, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
}

// Fixed sub resource on first mip level and layer
void pumex::setImageLayout( VkCommandBuffer cmdbuffer, VkImage image, VkImageAspectFlags aspectMask,
  VkImageLayout oldImageLayout, VkImageLayout newImageLayout)
{
  VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask   = aspectMask;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount   = 1;
    subresourceRange.layerCount   = 1;
  setImageLayout(cmdbuffer, image, aspectMask, oldImageLayout, newImageLayout, subresourceRange);
}

DescriptorSetValue Texture::getDescriptorSetValue(VkDevice device) const
{
  auto pddit = perDeviceData.find(device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Texture::getDescriptorSetValue : texture was not validated");

  return DescriptorSetValue(pddit->second.sampler, pddit->second.imageView, pddit->second.imageLayout);
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
