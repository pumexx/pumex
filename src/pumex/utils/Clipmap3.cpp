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

#include <pumex/utils/Clipmap3.h>
#include <pumex/RenderContext.h>
#include <pumex/utils/Log.h>

namespace pumex
{

Clipmap3::Clipmap3(uint32_t tq, uint32_t ts, VkClearValue iv, const ImageTraits& it, const SamplerTraits& tt, std::shared_ptr<DeviceMemoryAllocator> al)
  : Resource{ Resource::OnceForAllSwapChainImages }, textureQuantity { tq }, textureSize{ ts }, imageTraits{ it }, textureTraits{ tt }, allocator{ al }
{
  initValue = iv;
}

Clipmap3::~Clipmap3()
{
  for (auto& pdd : perDeviceData)
  {
    if (pdd.second.sampler != VK_NULL_HANDLE)
      vkDestroySampler(pdd.first, pdd.second.sampler, nullptr);
  }
}

Image* Clipmap3::getHandleImage(VkDevice device, uint32_t layer) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  if (pddit == perDeviceData.end())
    return nullptr;
  if(pddit->second.images.size()<layer)
    return nullptr;
  return pddit->second.images[layer].get();
}

void Clipmap3::validate(Device* device, CommandPool* commandPool, VkQueue queue)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (!pddit->second.images.empty())
    return;

  // Create sampler
  if( pddit->second.sampler == VK_NULL_HANDLE )
  {
    VkSamplerCreateInfo sampler{};
      sampler.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      sampler.magFilter               = textureTraits.magFilter;
      sampler.minFilter               = textureTraits.minFilter;
      sampler.mipmapMode              = textureTraits.mipmapMode;
      sampler.addressModeU            = textureTraits.addressModeU;
      sampler.addressModeV            = textureTraits.addressModeV;
      sampler.addressModeW            = textureTraits.addressModeW;
      sampler.mipLodBias              = textureTraits.mipLodBias;
      sampler.anisotropyEnable        = textureTraits.anisotropyEnable;
      sampler.maxAnisotropy           = textureTraits.maxAnisotropy;
      sampler.compareEnable           = textureTraits.compareEnable;
      sampler.compareOp               = textureTraits.compareOp;
      sampler.minLod                  = 0.0f;
      sampler.maxLod                  = 0.0f;
      sampler.borderColor             = textureTraits.borderColor;
      sampler.unnormalizedCoordinates = textureTraits.unnormalizedCoordinates;
    VK_CHECK_LOG_THROW( vkCreateSampler(device->device, &sampler, nullptr, &pddit->second.sampler) , "Cannot create sampler");
  }
  
  ImageTraits imTraits{ imageTraits };
  imTraits.usage = imTraits.usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // so that we are able to clear the image
  imTraits.extent      = VkExtent3D{ textureSize, textureSize, textureSize };
  imTraits.arrayLayers = 1; // just to be sure
  imTraits.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  for (uint32_t i = 0; i<textureQuantity; ++i)
    pddit->second.images.push_back(std::make_shared<Image>(device, imTraits, allocator));

  VkImageSubresourceRange subRes{};
  subRes.aspectMask     = imageTraits.aspectMask;
  subRes.baseMipLevel   = 0;
  subRes.levelCount     = imageTraits.mipLevels;
  subRes.baseArrayLayer = 0;
  subRes.layerCount     = imageTraits.arrayLayers;
  std::vector<VkImageSubresourceRange> subResources;
  subResources.push_back(subRes);

  auto cmdBuffer = device->beginSingleTimeCommands(commandPool);
  for (uint32_t i = 0; i < pddit->second.images.size(); ++i)
  {
    cmdBuffer->setImageLayout(*(pddit->second.images[i].get()), imageTraits.aspectMask, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    if(imageTraits.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT)
      cmdBuffer->cmdClearColorImage(*(pddit->second.images[i].get()), VK_IMAGE_LAYOUT_GENERAL, initValue, subResources);
    else
      cmdBuffer->cmdClearDepthStencilImage(*(pddit->second.images[i].get()), VK_IMAGE_LAYOUT_GENERAL, initValue, subResources);
  }
  device->endSingleTimeCommands(cmdBuffer, queue);

}

DescriptorSetValue Clipmap3::getDescriptorSetValue(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Clipmap3::getDescriptorSetValue() : texture was not validated");

  return DescriptorSetValue(pddit->second.sampler, pddit->second.images[0]->getImageView(), pddit->second.images[0]->getImageLayout());
}

}