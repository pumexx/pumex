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

#include <pumex/InputAttachment.h>
#include <pumex/Sampler.h>
#include <pumex/MemoryImage.h>
#include <pumex/RenderContext.h>
#include <pumex/Surface.h>
#include <pumex/FrameBuffer.h>

using namespace pumex;

InputAttachment::InputAttachment(const std::string& rn, std::shared_ptr<Sampler> s)
  : Resource{ pbPerSurface, swForEachImage }, resourceName{ rn }, sampler{ s }
{
  CHECK_LOG_THROW(resourceName.empty(), "InputAttachment : resourceName is not defined");
}

InputAttachment::~InputAttachment()
{
}

std::pair<bool, VkDescriptorType> InputAttachment::getDefaultDescriptorType()
{
  return{ true, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT };
}

void InputAttachment::validate(const RenderContext& renderContext)
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

  auto resourceAlias = renderContext.surface->workflowResults->resourceAlias.at(resourceName);
  imageView          = renderContext.surface->getRegisteredImageView(resourceAlias);
  registered         = false;

  if (!registered)
  {
    if (imageView != nullptr)
      imageView->addResource(shared_from_this());
    registered = true;
  }
  if (imageView != nullptr)
    imageView->validate(renderContext);
}

DescriptorValue InputAttachment::getDescriptorValue(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  VkSampler samp = (sampler != nullptr) ? sampler->getHandleSampler(renderContext) : VK_NULL_HANDLE;
  if (imageView != nullptr)
    return DescriptorValue(samp, imageView->getImageView(renderContext), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  // FIXME : CHECK_LOG_THROW ?
  return DescriptorValue(samp, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED);
}
