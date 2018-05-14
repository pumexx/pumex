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
#include <gli/texture.hpp>
#include <pumex/Export.h>
#include <pumex/Resource.h>

namespace pumex
{

class RenderContext;

// struct describing VkSampler / combined sampler 
struct PUMEX_EXPORT SamplerTraits
{
  explicit SamplerTraits(bool linearTiling = false, VkFilter magFilter = VK_FILTER_LINEAR, VkFilter minFilter = VK_FILTER_LINEAR, VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT, VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT, VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    float mipLodBias = 0.0f, VkBool32 anisotropyEnable = VK_TRUE, float maxAnisotropy = 8, VkBool32 compareEnable = false, VkCompareOp compareOp = VK_COMPARE_OP_NEVER, 
    float minLod = 0.0f, float maxLod = 10.0f, VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE, VkBool32 unnormalizedCoordinates = false);

  bool                    linearTiling            = false; 
  VkFilter                magFilter               = VK_FILTER_LINEAR;
  VkFilter                minFilter               = VK_FILTER_LINEAR;
  VkSamplerMipmapMode     mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  VkSamplerAddressMode    addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode    addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode    addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  float                   mipLodBias              = 0.0f;
  VkBool32                anisotropyEnable        = VK_TRUE;
  float                   maxAnisotropy           = 8;
  VkBool32                compareEnable           = false;
  VkCompareOp             compareOp               = VK_COMPARE_OP_NEVER;
  float                   minLod                  = 0.0f;
  float                   maxLod                  = 10.0f; // FIXME
  VkBorderColor           borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  VkBool32                unnormalizedCoordinates = false;
};

class PUMEX_EXPORT Sampler : public Resource
{
public:
  Sampler()                          = delete;
  // create single texture and clear it with specific value
  explicit Sampler(const SamplerTraits& samplerTraits, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage);
  Sampler(const Sampler&)            = delete;
  Sampler& operator=(const Sampler&) = delete;
  virtual ~Sampler();

  void                              setSamplerTraits(const SamplerTraits& samplerTraits);
  inline const SamplerTraits&       getSamplerTraits() const;

  void                              addResourceOwner(std::shared_ptr<Resource> resource);

  VkSampler                         getHandleSampler(const RenderContext& renderContext) const;

  void                              invalidateDescriptors() override;
  void                              notifyDescriptors(const RenderContext& renderContext) override;
  std::pair<bool, VkDescriptorType> getDefaultDescriptorType() override;
  void                              validate(const RenderContext& renderContext) override;
  DescriptorSetValue                getDescriptorSetValue(const RenderContext& renderContext) override;


protected:
  struct SamplerInternal
  {
    SamplerInternal()
      : sampler{ VK_NULL_HANDLE }
    {}
    VkSampler sampler;
  };
  typedef PerObjectData<SamplerInternal, uint32_t> SamplerData;

  std::unordered_map<uint32_t, SamplerData> perObjectData;
  SamplerTraits                             samplerTraits;
  // some resources ( InputAttachment, CombinedImageSampler ) use Sampler internally, so it cannot inform descriptors about changes itself - it must inform owners
  std::vector<std::weak_ptr<Resource>>      resourceOwners;

};

const SamplerTraits& Sampler::getSamplerTraits() const                     { return samplerTraits; }

}