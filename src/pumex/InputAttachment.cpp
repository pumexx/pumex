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
#include <pumex/Texture.h>
#include <pumex/RenderContext.h>
#include <pumex/Surface.h>
#include <pumex/FrameBuffer.h>

using namespace pumex;

InputAttachment::InputAttachment(const std::string& an, std::shared_ptr<Sampler> s)
  : Resource{ pbPerSurface, swForEachImage }, attachmentName{ an }, sampler{ s }
{
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
  if (sampler != nullptr)
    sampler->validate(renderContext);

  std::lock_guard<std::mutex> lock(mutex);
  if (renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }

  auto keyValue = getKeyID(renderContext, perObjectBehaviour);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, InputAttachmentData(renderContext, swForEachImage) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  auto frameBuffer                   = renderContext.surface->getFrameBuffer();
  pddit->second.commonData.imageView = frameBuffer->getImageView(attachmentName);
  pddit->second.commonData.imageView->addResource(shared_from_this());
  invalidateDescriptors();

  pddit->second.valid[activeIndex] = true;
}

void InputAttachment::invalidate()
{
  if (sampler != nullptr)
    sampler->invalidate();
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
    pdd.second.invalidate();
}

DescriptorSetValue InputAttachment::getDescriptorSetValue(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto keyValue = getKeyID(renderContext, perObjectBehaviour);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, InputAttachmentData(renderContext, swForEachImage) }).first;

  VkSampler samp = (sampler != nullptr) ? sampler->getHandleSampler(renderContext) : VK_NULL_HANDLE;
  VkImageView iv = (pddit->second.commonData.imageView != nullptr) ? pddit->second.commonData.imageView->getImageView(renderContext) : VK_NULL_HANDLE;
  return DescriptorSetValue(samp, iv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
