#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Device.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/utils/Log.h>

using namespace pumex;

DeviceMemoryBlock::DeviceMemoryBlock()
  : memory{ VK_NULL_HANDLE }, memoryOffset{ 0 }, size{ 0 }
{
}


DeviceMemoryBlock::DeviceMemoryBlock(VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s)
  : memory{ m }, memoryOffset{ o }, size{ s }
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

DeviceMemoryBlock DeviceMemoryAllocator::allocate(std::shared_ptr<Device> device, VkMemoryRequirements memoryRequirements)
{
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
  auto pddit = perDeviceData.find(device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Cannot deallocate memory - device memory was never allocated");
  allocationStrategy->deallocate(pddit->second.freeBlocks, block);
}

FirstFitAllocationStrategy::FirstFitAllocationStrategy()
{
}

DeviceMemoryBlock FirstFitAllocationStrategy::allocate(VkDeviceMemory storageMemory, std::list<FreeBlock>& freeBlocks, VkMemoryRequirements memoryRequirements)
{
  auto it = freeBlocks.begin();
  for (; it != freeBlocks.end(); ++it)
  {
    if (it->size >= memoryRequirements.size)
      break;
  }
  CHECK_LOG_THROW(it == freeBlocks.end(), "no more memory in FirstFitAllocationStrategy");

  // FIXME - ALIGNMENT !!!
  DeviceMemoryBlock block(storageMemory, it->offset, memoryRequirements.size);
  it->offset += memoryRequirements.size;
  it->size   -= memoryRequirements.size;
  if (it->size == 0)
    freeBlocks.erase(it);
  return block;
}

void FirstFitAllocationStrategy::deallocate(std::list<FreeBlock>& freeBlocks, const DeviceMemoryBlock& block)
{
  FreeBlock fBlock(block.memoryOffset, block.size);
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
