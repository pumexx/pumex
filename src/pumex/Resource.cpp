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

#include <pumex/Resource.h>
#include <algorithm>
#include <pumex/Descriptor.h>
#include <pumex/utils/Log.h>
#include <pumex/RenderContext.h>
#include <pumex/Device.h>
#include <pumex/Surface.h>

using namespace pumex;

DescriptorValue::DescriptorValue()
  : vType{ Undefined }
{
}

DescriptorValue::DescriptorValue(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
  : vType{ Buffer }
{
  bufferInfo.buffer = buffer;
  bufferInfo.offset = offset;
  bufferInfo.range = range;
}

DescriptorValue::DescriptorValue(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
  : vType{ Image }
{
  imageInfo.sampler     = sampler;
  imageInfo.imageView   = imageView;
  imageInfo.imageLayout = imageLayout;
}

Resource::Resource(PerObjectBehaviour pob, SwapChainImageBehaviour scib)
  : perObjectBehaviour{ pob }, swapChainImageBehaviour { scib }, activeCount{ 1 }
{

}

Resource::~Resource()
{
}

void Resource::addDescriptor(std::shared_ptr<Descriptor> descriptor)
{
  descriptors.push_back(descriptor);
}

void Resource::removeDescriptor(std::shared_ptr<Descriptor> descriptor)
{
  auto it = std::find_if(begin(descriptors), end(descriptors), [descriptor](std::weak_ptr<Descriptor> p) -> bool { return p.lock() == descriptor; });
  if (it != end(descriptors))
    descriptors.erase(it);
}

void Resource::invalidateDescriptors()
{
  for (auto& ds : descriptors)
    ds.lock()->invalidateDescriptorSet();
}

void Resource::notifyDescriptors(const RenderContext& renderContext)
{
  for (auto& ds : descriptors)
    ds.lock()->notifyDescriptorSet(renderContext);
}

std::pair<bool, VkDescriptorType> Resource::getDefaultDescriptorType()
{
  CHECK_LOG_THROW(true, "This resource does not have default descriptor type");
  return{ false,VK_DESCRIPTOR_TYPE_MAX_ENUM };
}
