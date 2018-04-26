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

#include <pumex/SampledImage.h>
#include <pumex/Texture.h>

using namespace pumex;

SampledImage::SampledImage(std::shared_ptr<ImageView> iv)
  : Resource{ iv->texture->getPerObjectBehaviour(), iv->texture->getSwapChainImageBehaviour() }, imageView{ iv }
{
  imageView->addResource(shared_from_this());
}

SampledImage::~SampledImage()
{
}

std::pair<bool, VkDescriptorType> SampledImage::getDefaultDescriptorType()
{
  return{ true, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE };
}

void SampledImage::validate(const RenderContext& renderContext)
{
  imageView->validate(renderContext);
}

void SampledImage::invalidate()
{
  imageView->invalidate();
  invalidateDescriptors();
}

DescriptorSetValue SampledImage::getDescriptorSetValue(const RenderContext& renderContext)
{
  return DescriptorSetValue(VK_NULL_HANDLE, imageView->getImageView(renderContext), VK_IMAGE_LAYOUT_UNDEFINED);
}
