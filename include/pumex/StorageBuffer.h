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
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/Resource.h>
#include <pumex/MemoryBuffer.h>

namespace pumex
{

// Resource that stores information about storage buffer
// May be referenced in glsl shader as for example : layout (std430,binding = 1) readonly buffer
class PUMEX_EXPORT StorageBuffer : public Resource
{
public:
  StorageBuffer()                                = delete;
  StorageBuffer(std::shared_ptr<MemoryBuffer> memoryBuffer);
  StorageBuffer(const std::string& resourceName);
  StorageBuffer(const StorageBuffer&)            = delete;
  StorageBuffer& operator=(const StorageBuffer&) = delete;
  StorageBuffer(StorageBuffer&&)                 = delete;
  StorageBuffer& operator=(StorageBuffer&&)      = delete;
  virtual ~StorageBuffer();

  std::pair<bool, VkDescriptorType> getDefaultDescriptorType() override;
  void                              validate(const RenderContext& renderContext) override;
  DescriptorValue                   getDescriptorValue(const RenderContext& renderContext) override;

  std::shared_ptr<MemoryBuffer> memoryBuffer;
protected:
  std::string                   resourceName;
  bool                          registered = false;
};

}
