#pragma once
#include <memory>
#include <list>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

class Device;

struct PUMEX_EXPORT DeviceMemoryBlock
{
  DeviceMemoryBlock();
  DeviceMemoryBlock(VkDeviceMemory memory, VkDeviceSize realOffset, VkDeviceSize alignedOffset, VkDeviceSize realSize, VkDeviceSize alignedSize);

  VkDeviceMemory memory;
  VkDeviceSize   realOffset;
  VkDeviceSize   alignedOffset;
  VkDeviceSize   realSize;
  VkDeviceSize   alignedSize;
};

struct FreeBlock
{
  FreeBlock(VkDeviceSize offset, VkDeviceSize size);
  VkDeviceSize offset;
  VkDeviceSize size;
};

class PUMEX_EXPORT AllocationStrategy
{
public:
  virtual DeviceMemoryBlock allocate(VkDeviceMemory storageMemory, std::list<FreeBlock>& freeBlocks, VkMemoryRequirements memoryRequirements) = 0;
  virtual void deallocate(std::list<FreeBlock>& freeBlocks, const DeviceMemoryBlock& block) = 0;
};

class PUMEX_EXPORT DeviceMemoryAllocator
{
public:
  enum EnumStrategy { FIRST_FIT };
  DeviceMemoryAllocator()                                        = delete;
  explicit DeviceMemoryAllocator(VkMemoryPropertyFlags propertyFlags, VkDeviceSize size, EnumStrategy strategy);
  DeviceMemoryAllocator(const DeviceMemoryAllocator&)            = delete;
  DeviceMemoryAllocator& operator=(const DeviceMemoryAllocator&) = delete;
  ~DeviceMemoryAllocator();


  DeviceMemoryBlock allocate(std::shared_ptr<Device> device, VkMemoryRequirements memoryRequirements);
  void deallocate(VkDevice device, const DeviceMemoryBlock& block);

  inline VkDeviceSize getMemorySize() const;

protected:
  VkMemoryPropertyFlags propertyFlags;
  VkDeviceSize          size;
  std::unique_ptr<AllocationStrategy> allocationStrategy;


  struct PerDeviceData
  {
    PerDeviceData()
    {
    }
    VkDeviceMemory       storageMemory = VK_NULL_HANDLE;
    std::list<FreeBlock> freeBlocks;
  };
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

VkDeviceSize DeviceMemoryAllocator::getMemorySize() const { return size; }



class PUMEX_EXPORT FirstFitAllocationStrategy : public AllocationStrategy
{
public:
  FirstFitAllocationStrategy();
  DeviceMemoryBlock allocate(VkDeviceMemory storageMemory, std::list<FreeBlock>& freeBlocks, VkMemoryRequirements memoryRequirements) override;
  void deallocate(std::list<FreeBlock>& freeBlocks, const DeviceMemoryBlock& block) override;
};



}
