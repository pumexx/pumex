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

#include <pumex/CombinedImageSampler.h>
#include <pumex/MemoryImage.h>
#include <pumex/Sampler.h>
#include <pumex/RenderContext.h>
#include <pumex/RenderWorkflow.h>
#include <pumex/Surface.h>
#include <pumex/utils/Log.h>

using namespace pumex;

CombinedImageSampler::CombinedImageSampler(std::shared_ptr<ImageView> iv, std::shared_ptr<Sampler> s)
  : Resource{ iv->memoryImage->getPerObjectBehaviour(), iv->memoryImage->getSwapChainImageBehaviour() }, imageView{ iv }, resourceName{}, sampler { s }
{
  CHECK_LOG_THROW((iv->memoryImage->getImageTraits().usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0, "CombinedImageSampler resource connected to a texture that does not have VK_IMAGE_USAGE_SAMPLED_BIT");
}

CombinedImageSampler::CombinedImageSampler(const std::string& rn, std::shared_ptr<Sampler> s)
  : Resource{ pbPerSurface, swForEachImage }, imageView{}, resourceName{ rn }, sampler{ s }
{
  CHECK_LOG_THROW(resourceName.empty(), "CombinedImageSampler : resourceName is not defined");
}

CombinedImageSampler::~CombinedImageSampler()
{
}

std::pair<bool, VkDescriptorType> CombinedImageSampler::getDefaultDescriptorType()
{
  return{ true, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
}

void CombinedImageSampler::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (!samplerRegistered)
  {
    if (sampler != nullptr)
      sampler->addResourceOwner(shared_from_this());
    samplerRegistered = true;
  }
  if (sampler != nullptr)
    sampler->validate(renderContext);

  if (!resourceName.empty())
  {
    auto resourceAlias = renderContext.surface->workflowResults->resourceAlias.at(resourceName);
    imageView          = renderContext.surface->getRegisteredImageView(resourceAlias);
    registered         = false;
  }
  if (!registered)
  {
    if (imageView != nullptr)
      imageView->addResource(shared_from_this());
    registered = true;
  }
  if (imageView != nullptr)
    imageView->validate(renderContext);
}

DescriptorValue CombinedImageSampler::getDescriptorValue(const RenderContext& renderContext)
{
  return DescriptorValue(sampler->getHandleSampler(renderContext), imageView->getImageView(renderContext), VK_IMAGE_LAYOUT_UNDEFINED);
}
