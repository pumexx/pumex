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

#pragma once
#include <memory>
#include <vector>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/PerObjectData.h>

namespace pumex
{

class  Descriptor;
class  RenderContext;

struct PUMEX_EXPORT DescriptorValue
{
  enum Type { Undefined, Image, Buffer };
  DescriptorValue();
  DescriptorValue(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
  DescriptorValue(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);

  Type vType;
  union
  {
    VkDescriptorBufferInfo bufferInfo;
    VkDescriptorImageInfo  imageInfo;
  };
};

// Resource is an object stored in a descriptor ( SampledImage, UniformBuffer, etc. )
class PUMEX_EXPORT Resource : public std::enable_shared_from_this<Resource>
{
public:
  Resource(PerObjectBehaviour perObjectBehaviour, SwapChainImageBehaviour swapChainImageBehaviour);
  virtual ~Resource();

  void                                     addDescriptor(std::shared_ptr<Descriptor> descriptor);
  void                                     removeDescriptor(std::shared_ptr<Descriptor> descriptor);
  // invalidateDescriptors() is called to inform the scenegraph that validate() needs to be called
  virtual void                             invalidateDescriptors();
  // notifyDescriptors() is called from within validate() when some serious change in resource occured
  // ( getDescriptorValue() will return new values, so vkUpdateDescriptorSets must be called by DescriptorSet ).
  virtual void                             notifyDescriptors(const RenderContext& renderContext);

  virtual std::pair<bool,VkDescriptorType> getDefaultDescriptorType();
  virtual void                             validate(const RenderContext& renderContext) = 0;
  virtual DescriptorValue                  getDescriptorValue(const RenderContext& renderContext) = 0;
protected:
  mutable std::mutex                       mutex;
  std::vector<std::weak_ptr<Descriptor>>   descriptors;
  PerObjectBehaviour                       perObjectBehaviour;
  SwapChainImageBehaviour                  swapChainImageBehaviour;
  uint32_t                                 activeCount;
};

}
