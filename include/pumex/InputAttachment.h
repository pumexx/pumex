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
#include <unordered_map>
#include <pumex/Export.h>
#include <pumex/Resource.h>

namespace pumex
{

class Sampler;
class ImageView;

// Resource that stores information about input attachment defined in a framebuffer
// May be referenced in glsl shader as for example : layout(input_attachment_index=0, binding = 2) uniform subpassInputMS
class PUMEX_EXPORT InputAttachment : public Resource
{
public:
  InputAttachment(const std::string& resourceName, std::shared_ptr<Sampler> sampler = nullptr);
  virtual ~InputAttachment();

  std::pair<bool, VkDescriptorType> getDefaultDescriptorType() override;
  void                              validate(const RenderContext& renderContext) override;
  DescriptorValue                   getDescriptorValue(const RenderContext& renderContext) override;

protected:
  std::shared_ptr<ImageView> imageView;
  std::string                resourceName;
  std::shared_ptr<Sampler>   sampler;
  bool                       registered        = false;
  bool                       samplerRegistered = false;
};

}
