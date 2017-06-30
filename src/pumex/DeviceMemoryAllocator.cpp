//
// Copyright(c) 2017 Pawe³ Ksiê¿opolski ( pumexx )
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

#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Device.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/utils/Log.h>

using namespace pumex;

DeviceMemoryBlock::DeviceMemoryBlock()
  : memory{ VK_NULL_HANDLE }, realOffset{ 0 }, alignedOffset{ 0 }, realSize{ 0 }, alignedSize{ 0 }
{
}


DeviceMemoryBlock::DeviceMemoryBlock(VkDeviceMemory m, VkDeviceSize ro, VkDeviceSize ao, VkDeviceSize rs, VkDeviceSize as)
  : memory{ m }, realOffset{ ro }, alignedOffset{ ao }, realSize{ rs }, alignedSize{ as }
{

}

FreeBlock::FreeBlock(VkDeviceSize o, VkDeviceSize s)
  : offset{ o }, size{ s }
{

}


DeviceMemoryAllocator::DeviceMemoryAllocator(VkMemoryPropertyFlags pf, VkDeviceSize s, EnumStrategy st)
  : propertyFlags{ pf }, size{ s }
{
  switch (st)
  {
  case FIRST_FIT: allocationStrategy = std::make_unique<FirstFitAllocationStrategy>(); break;
  }

}

DeviceMemoryAllocator::~DeviceMemoryAllocator()
{
  for (auto& pddit : perDeviceData)
    vkFreeMemory(pddit.first, pddit.second.storageMemory, nullptr);
}

DeviceMemoryBlock DeviceMemoryAllocator::allocate(Device* device, VkMemoryRequirements memoryRequirements)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (pddit->second.storageMemory == VK_NULL_HANDLE)
  {
    VkMemoryAllocateInfo memAlloc{};
      memAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      memAlloc.allocationSize  = size;
      memAlloc.memoryTypeIndex = device->physical.lock()->getMemoryType(memoryRequirements.memoryTypeBits, propertyFlags);
    VK_CHECK_LOG_THROW(vkAllocateMemory(device->device, &memAlloc, nullptr, &pddit->second.storageMemory), "Cannot allocate memory in DeviceMemoryAllocator");
    pddit->second.freeBlocks.push_front(FreeBlock(0, size));
  }
  return allocationStrategy->allocate(pddit->second.storageMemory, pddit->second.freeBlocks, memoryRequirements);
}

void DeviceMemoryAllocator::deallocate(VkDevice device, const DeviceMemoryBlock& block)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Cannot deallocate memory - device memory was never allocated");
  allocationStrategy->deallocate(pddit->second.freeBlocks, block);
}

void DeviceMemoryAllocator::copyToDeviceMemory(Device* device, VkDeviceSize offset, void* data, VkDeviceSize size, VkMemoryMapFlags flags) 
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "DeviceMemoryAllocator::copyToDeviceMemory() : cannot copy to memory that not have been allocated yet");
  uint8_t *pData;
  VK_CHECK_LOG_THROW(vkMapMemory(device->device, pddit->second.storageMemory, offset, size, 0, (void **)&pData), "Cannot map memory");
  memcpy(pData, data, size);
  vkUnmapMemory(device->device, pddit->second.storageMemory);
}

void DeviceMemoryAllocator::bindBufferMemory(Device* device, VkBuffer buffer, VkDeviceSize offset)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(device->device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "DeviceMemoryAllocator::bindBufferMemory() : cannot bind memory that not have been allocated yet");
  VK_CHECK_LOG_THROW(vkBindBufferMemory(device->device, buffer, pddit->second.storageMemory, offset), "Cannot bind memory to buffer");
}

FirstFitAllocationStrategy::FirstFitAllocationStrategy()
{
}

DeviceMemoryBlock FirstFitAllocationStrategy::allocate(VkDeviceMemory storageMemory, std::list<FreeBlock>& freeBlocks, VkMemoryRequirements memoryRequirements)
{
  auto it = freeBlocks.begin();
  VkDeviceSize additionalSize;
  for (; it != freeBlocks.end(); ++it)
  {
    VkDeviceSize modd = it->offset % memoryRequirements.alignment;
    additionalSize = (modd == 0) ? 0 : memoryRequirements.alignment - modd;
    if (it->size >= memoryRequirements.size + additionalSize)
      break;
  }
  CHECK_LOG_THROW(it == freeBlocks.end(), "memory allocation failed in FirstFitAllocationStrategy");

  DeviceMemoryBlock block(storageMemory, it->offset, it->offset + additionalSize, memoryRequirements.size, memoryRequirements.size + additionalSize);
  it->offset += memoryRequirements.size + additionalSize;
  it->size   -= memoryRequirements.size + additionalSize;
  if (it->size == 0)
    freeBlocks.erase(it);
  return block;
}

void FirstFitAllocationStrategy::deallocate(std::list<FreeBlock>& freeBlocks, const DeviceMemoryBlock& block)
{
  FreeBlock fBlock(block.realOffset, block.realSize);
  if (freeBlocks.empty())
  {
    freeBlocks.push_back(fBlock);
    return;
  }

  auto it = freeBlocks.begin();
  for (; it != freeBlocks.end(); ++it)
  {
    // check if a new block lies before an existing block 
    if (it->offset >= fBlock.offset + fBlock.size)
    {
      // check if a new block may be added in front of an existing block
      if (it->offset == fBlock.offset + fBlock.size)
      {
        it->offset -= fBlock.size;
        it->size += fBlock.size;
      }
      else
        it = freeBlocks.insert(it, fBlock);
      if (it == freeBlocks.begin())
        return;
      it--;
      break;
    }
    // check if a new block fits at the end of an existing block
    if (fBlock.offset == it->offset + it->size)
    {
      it->size += fBlock.size;
      break;
    }
  }
  if (it == freeBlocks.end())
  {
    freeBlocks.push_back(fBlock);
    return;
  }
  // check if it may be coalesced to the next block 
  auto nit = it;
  ++nit;
  if (nit != freeBlocks.end() && ((it->offset + it->size) == nit->offset))
  {
    it->size += nit->size;
    freeBlocks.erase(nit);
  }
}
