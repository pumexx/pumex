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

#include <pumex/UniformBuffer.h>
#include <pumex/utils/Log.h>

using namespace pumex;

UniformBuffer::UniformBuffer(std::shared_ptr<MemoryBuffer> mb)
  : Resource{ mb->getPerObjectBehaviour(), mb->getSwapChainImageBehaviour() }, memoryBuffer{ mb }
{
  CHECK_LOG_THROW((mb->getBufferUsage() & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) == 0, "UniformBuffer resource connected to a memory buffer that does not have VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT");
}

UniformBuffer::~UniformBuffer()
{
}

std::pair<bool, VkDescriptorType> UniformBuffer::getDefaultDescriptorType()
{
  return{ true, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER };
}

void UniformBuffer::validate(const RenderContext& renderContext)
{
  if (!registered)
  {
    memoryBuffer->addResource(shared_from_this());
    registered = true;
  }
  memoryBuffer->validate(renderContext);
}

DescriptorSetValue UniformBuffer::getDescriptorSetValue(const RenderContext& renderContext)
{
  return DescriptorSetValue(memoryBuffer->getHandleBuffer(renderContext), 0, memoryBuffer->getDataSize());
}
