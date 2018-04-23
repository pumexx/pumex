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

#include <pumex/Sampler.h>
#include <pumex/Device.h>
#include <pumex/RenderContext.h>
#include <pumex/utils/Log.h>

using namespace pumex;

SamplerTraits::SamplerTraits(bool lt, VkFilter maf, VkFilter mif, VkSamplerMipmapMode mm, VkSamplerAddressMode au, VkSamplerAddressMode av, VkSamplerAddressMode aw, float mlb, VkBool32 ae,
  float maa, VkBool32 ce, VkCompareOp co, float mil, float mal, VkBorderColor bc, VkBool32 uc)
  : linearTiling{ lt }, magFilter{ maf }, minFilter{ mif }, mipmapMode{ mm }, addressModeU{ au }, addressModeV{ av }, addressModeW{ aw }, mipLodBias{ mlb }, 
  anisotropyEnable{ ae }, maxAnisotropy{ maa }, compareEnable{ ce }, compareOp{ co }, minLod{ mil }, maxLod{ mal }, borderColor{ bc }, unnormalizedCoordinates{ uc }
{
}

Sampler::Sampler(const SamplerTraits& st, Resource::SwapChainImageBehaviour scib)
  : Resource{ scib }, samplerTraits{ st }
{

}

Sampler::~Sampler()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    for (uint32_t i = 0; i < activeCount; ++i)
        vkDestroySampler(pdd.first, pdd.second.sampler[i], nullptr);
}


VkSampler Sampler::getHandleSampler(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == end(perDeviceData))
    return VK_NULL_HANDLE;
  return pddit->second.sampler[renderContext.activeIndex % activeCount];
}

std::pair<bool, VkDescriptorType> Sampler::getDefaultDescriptorType()
{
  return{ true, VK_DESCRIPTOR_TYPE_SAMPLER };
}

void Sampler::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (swapChainImageBehaviour == Resource::ForEachSwapChainImage && renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perDeviceData)
      pdd.second.resize(activeCount);
  }
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == end(perDeviceData))
    pddit = perDeviceData.insert({ renderContext.vkDevice, PerDeviceData(activeCount) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  // delete old sampler
  if (pddit->second.sampler[activeIndex] != VK_NULL_HANDLE)
  {
    vkDestroySampler(pddit->first, pddit->second.sampler[activeIndex], nullptr);
    pddit->second.sampler[activeIndex] = VK_NULL_HANDLE;
  }

  // Create sampler
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
    sampler.minLod                  = samplerTraits.minLod;
    sampler.maxLod                  = samplerTraits.maxLod;
    sampler.borderColor             = samplerTraits.borderColor;
    sampler.unnormalizedCoordinates = samplerTraits.unnormalizedCoordinates;
  VK_CHECK_LOG_THROW(vkCreateSampler(renderContext.vkDevice, &sampler, nullptr, &pddit->second.sampler[activeIndex]), "Cannot create sampler");

  pddit->second.valid[activeIndex] = true;
}

void Sampler::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    std::fill(begin(pdd.second.valid), end(pdd.second.valid), false);
  invalidateDescriptors();
}

DescriptorSetValue Sampler::getDescriptorSetValue(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  CHECK_LOG_THROW(pddit == end(perDeviceData), "Texture::getDescriptorSetValue() : texture was not validated");
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  return DescriptorSetValue(pddit->second.sampler[activeIndex], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED);
}
