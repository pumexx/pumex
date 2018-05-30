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

Sampler::Sampler(const SamplerTraits& st, SwapChainImageBehaviour scib)
  : Resource{ pbPerDevice, scib }, samplerTraits{ st }
{

}

Sampler::~Sampler()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
        vkDestroySampler(pdd.second.device, pdd.second.data[i].sampler, nullptr);
}

void Sampler::setSamplerTraits(const SamplerTraits& st)
{
  samplerTraits = st;
  invalidateDescriptors();
}

void Sampler::addResourceOwner(std::shared_ptr<Resource> resource) 
{ 
  if (std::find_if(begin(resourceOwners), end(resourceOwners), [&resource](std::weak_ptr<Resource> r) { return !r.expired() && r.lock().get() == resource.get(); }) == end(resourceOwners))
    resourceOwners.push_back(resource);
}

void Sampler::invalidateDescriptors()
{
  // inform all owners that they need to invalidated
  auto eit = std::remove_if(begin(resourceOwners), end(resourceOwners), [](std::weak_ptr<Resource> r) { return r.expired();  });
  for (auto it = begin(resourceOwners); it != eit; ++it)
    it->lock()->invalidateDescriptors();
  resourceOwners.erase(eit, end(resourceOwners));
  // notify sampler's own descriptors
  Resource::invalidateDescriptors();
}

void Sampler::notifyDescriptors(const RenderContext& renderContext)
{
  // inform all owners about crucial sampler changes
  auto eit = std::remove_if(begin(resourceOwners), end(resourceOwners), [](std::weak_ptr<Resource> r) { return r.expired();  });
  for (auto it = begin(resourceOwners); it != eit; ++it)
    it->lock()->notifyDescriptors(renderContext);
  resourceOwners.erase(eit, end(resourceOwners));
  // notify sampler's own descriptors
  Resource::notifyDescriptors(renderContext);
}

VkSampler Sampler::getHandleSampler(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto keyValue = getKeyID(renderContext, perObjectBehaviour);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    return VK_NULL_HANDLE;
  return pddit->second.data[renderContext.activeIndex % activeCount].sampler;
}

std::pair<bool, VkDescriptorType> Sampler::getDefaultDescriptorType()
{
  return{ true, VK_DESCRIPTOR_TYPE_SAMPLER };
}

void Sampler::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (swapChainImageBehaviour == swForEachImage && renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKeyID(renderContext, perObjectBehaviour);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, SamplerData(renderContext, swapChainImageBehaviour) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  // delete old sampler
  if (pddit->second.data[activeIndex].sampler != VK_NULL_HANDLE)
  {
    vkDestroySampler(pddit->second.device, pddit->second.data[activeIndex].sampler, nullptr);
    pddit->second.data[activeIndex].sampler = VK_NULL_HANDLE;
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
  VK_CHECK_LOG_THROW(vkCreateSampler(pddit->second.device, &sampler, nullptr, &pddit->second.data[activeIndex].sampler), "Cannot create sampler");

  notifyDescriptors(renderContext);
  pddit->second.valid[activeIndex] = true;
}

DescriptorValue Sampler::getDescriptorValue(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(getKeyID(renderContext,perObjectBehaviour));
  CHECK_LOG_THROW(pddit == end(perObjectData), "Sampler::getDescriptorValue() : sampler  was not validated");
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  return DescriptorValue(pddit->second.data[activeIndex].sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED);
}
