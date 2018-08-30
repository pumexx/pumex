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

#include <pumex/MemoryObjectBarrier.h>

using namespace pumex;

MemoryObjectBarrier::MemoryObjectBarrier()
: objectType(MemoryObject::moUndefined), srcAccessMask{ 0 }, dstAccessMask{ 0 }, srcQueueFamilyIndex{ 0 }, dstQueueFamilyIndex{ 0 }
{
}

MemoryObjectBarrier::MemoryObjectBarrier(VkAccessFlags sam, VkAccessFlags dam, uint32_t sqfi, uint32_t dqfi, std::shared_ptr<MemoryObject> mo, VkImageLayout ol, VkImageLayout nl, const ImageSubresourceRange& ir)
  : objectType(MemoryObject::moImage), srcAccessMask{ sam }, dstAccessMask{ dam }, srcQueueFamilyIndex{ sqfi }, dstQueueFamilyIndex{ dqfi }, memoryObject{ mo }, oldLayout{ ol }, newLayout{ nl }, imageRange{ ir }, bufferRange{}
{
}

MemoryObjectBarrier::MemoryObjectBarrier(VkAccessFlags sam, VkAccessFlags dam, uint32_t sqfi, uint32_t dqfi, std::shared_ptr<MemoryObject> mo, const BufferSubresourceRange& br)
  : objectType(MemoryObject::moBuffer), srcAccessMask{ sam }, dstAccessMask{ dam }, srcQueueFamilyIndex{ sqfi }, dstQueueFamilyIndex{ dqfi }, memoryObject{ mo }, oldLayout{ VK_IMAGE_LAYOUT_UNDEFINED }, newLayout{ VK_IMAGE_LAYOUT_UNDEFINED }, imageRange{}, bufferRange{ br }
{
}

MemoryObjectBarrier::~MemoryObjectBarrier()
{
}

MemoryObjectBarrierGroup::MemoryObjectBarrierGroup(VkPipelineStageFlags ssm, VkPipelineStageFlags dsm, VkDependencyFlags d)
  : srcStageMask{ ssm }, dstStageMask{ dsm }, dependencyFlags{ d }
{
}
