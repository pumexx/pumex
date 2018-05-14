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
#include <memory>
#include <list>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/PerObjectData.h>
#include <pumex/Device.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

namespace pumex
{

class CommandBufferSource;
class Resource;
class RenderContext;
class CommandBuffer;
class BufferView;

struct PUMEX_EXPORT BufferSubresourceRange
{
  BufferSubresourceRange(VkDeviceSize offset, VkDeviceSize range);

  bool contains(const BufferSubresourceRange& subRange) const;

  VkDeviceSize offset;
  VkDeviceSize range;
};

class PUMEX_EXPORT MemoryBuffer
{
public:
  MemoryBuffer()                               = delete;
  explicit MemoryBuffer(std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlags bufferUsage, PerObjectBehaviour perObjectBehaviour = pbPerDevice, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage, bool sameDataPerObject = true, bool useSetDataMethods = true);
  MemoryBuffer(const MemoryBuffer&)            = delete;
  MemoryBuffer& operator=(const MemoryBuffer&) = delete;
  virtual ~MemoryBuffer();

  inline const PerObjectBehaviour&              getPerObjectBehaviour() const;
  inline const SwapChainImageBehaviour&         getSwapChainImageBehaviour() const;
  inline std::shared_ptr<DeviceMemoryAllocator> getAllocator() const;
  inline VkBufferUsageFlags                     getBufferUsage() const;

  VkBuffer                                      getHandleBuffer(const RenderContext& renderContext) const;
  size_t                                        getBufferSize(const RenderContext& renderContext) const;

  void                                          validate(const RenderContext& renderContext);

  void                                          addCommandBufferSource(std::shared_ptr<CommandBufferSource> cbSource);
  void                                          notifyCommandBufferSources(const RenderContext& renderContext);

  void                                          addResource(std::shared_ptr<Resource> resource);
  void                                          invalidateResources();
  void                                          notifyResources(const RenderContext& renderContext);

  void                                          addBufferView(std::shared_ptr<BufferView> bufferView);
  void                                          notifyBufferViews(const RenderContext& renderContext, const BufferSubresourceRange& range);

  struct MemoryBufferInternal
  {
    MemoryBufferInternal()
    : buffer{ VK_NULL_HANDLE }, memoryBlock()
    {
    }
    VkBuffer           buffer;
    DeviceMemoryBlock  memoryBlock;
  };
  struct Operation
  {
    enum Type { SetBufferSize, SetData };
    Operation(MemoryBuffer* o, Type t, const BufferSubresourceRange& r, uint32_t ac)
      : owner{ o }, type{ t }, bufferRange{ r }
    {
      resize(ac);
    }
    void resize(uint32_t ac)
    {
      updated.resize(ac, false);
    }
    bool allUpdated()
    {
      for (auto& u : updated)
        if (!u)
          return false;
      return true;
    }
    // perform() should return true when it added commands to commandBuffer
    virtual bool perform(const RenderContext& renderContext, MemoryBufferInternal& internals, std::shared_ptr<CommandBuffer> commandBuffer) = 0;
    virtual void releaseResources(const RenderContext& renderContext)
    {
    }

    MemoryBuffer*          owner;
    Type                   type;
    BufferSubresourceRange bufferRange;
    std::vector<bool>      updated;
  };

  virtual void*  getDataPointer() = 0;
  virtual size_t getDataSize() = 0;
  virtual void   sendDataToBuffer(uint32_t key, VkDevice device, VkSurfaceKHR surface) = 0;
protected:
  struct MemoryBufferLoadData
  {
    std::list<std::shared_ptr<Operation>> bufferOperations;
  };
  typedef PerObjectData<MemoryBufferInternal, MemoryBufferLoadData> MemoryBufferData;

  std::unordered_map<uint32_t, MemoryBufferData>  perObjectData;
  mutable std::mutex                              mutex;
  PerObjectBehaviour                              perObjectBehaviour;
  SwapChainImageBehaviour                         swapChainImageBehaviour;
  bool                                            sameDataPerObject;
  std::shared_ptr<DeviceMemoryAllocator>          allocator;
  VkBufferUsageFlags                              bufferUsage;
  uint32_t                                        activeCount;
  // objects that may own a buffer and must be informed when some changes happen
  std::vector<std::weak_ptr<CommandBufferSource>> commandBufferSources;
  std::vector<std::weak_ptr<Resource>>            resources;
  std::vector<std::weak_ptr<BufferView>>          bufferViews;
};

template <typename T>
class Buffer : public MemoryBuffer
{
public:
  Buffer() = delete;
  explicit Buffer(std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlags bufferUsage, PerObjectBehaviour perObjectBehaviour = pbPerDevice, SwapChainImageBehaviour swapChainImageBehaviour = swForEachImage, bool useSetDataMethods = true);
  explicit Buffer(std::shared_ptr<T> data, std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlags bufferUsage, PerObjectBehaviour perObjectBehaviour, SwapChainImageBehaviour swapChainImageBehaviour);
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  virtual ~Buffer();

  void setBufferSize(size_t bufferSize);
  void setBufferSize(Surface* surface, size_t bufferSize);
  void setBufferSize(Device* device, size_t bufferSize);

  void invalidateData();
  void setData(const T& data);
  void setData(Surface* surface, std::shared_ptr<T> data);
  void setData(Device* device, std::shared_ptr<T> data);
  void setData(Surface* surface, const T& data);
  void setData(Device* device, const T& data);

  void*  getDataPointer() override;
  size_t getDataSize() override;
  void   sendDataToBuffer(uint32_t key, VkDevice device, VkSurfaceKHR surface) override;

protected:
  std::shared_ptr<T> data;

  void internalSetBufferSize(uint32_t key, VkDevice device, VkSurfaceKHR surface, size_t bufferSize);
  void internalSetData(uint32_t key, VkDevice device, VkSurfaceKHR surface, std::shared_ptr<T> data);
};

class PUMEX_EXPORT BufferView : public std::enable_shared_from_this<BufferView>
{
public:
  BufferView()                             = delete;
  BufferView(std::shared_ptr<MemoryBuffer> memBuffer, const BufferSubresourceRange& subresourceRange, VkFormat format);
  BufferView(const BufferView&)            = delete;
  BufferView& operator=(const BufferView&) = delete;
  virtual ~BufferView();

  VkBuffer      getHandleBuffer(const RenderContext& renderContext) const;
  VkBufferView  getBufferView(const RenderContext& renderContext) const;

  void          validate(const RenderContext& renderContext);
  void          notifyBufferView(const RenderContext& renderContext);

  void          addResource(std::shared_ptr<Resource> resource);

  std::shared_ptr<MemoryBuffer> memBuffer;
  BufferSubresourceRange        subresourceRange;
  VkFormat                      format;
protected:
  struct BufferViewInternal
  {
    BufferViewInternal()
      : bufferView{ VK_NULL_HANDLE }
    {}
    VkBufferView bufferView;
  };
  typedef PerObjectData<BufferViewInternal, uint32_t> BufferViewData;
  mutable std::mutex                           mutex;
  std::vector<std::weak_ptr<Resource>>         resources;
  std::unordered_map<uint32_t, BufferViewData> perObjectData;
  uint32_t                                     activeCount;
  bool                                         registered = false;

  void notifyResources(const RenderContext& renderContext);
};

template<typename T>
struct SetBufferSizeOperation : public MemoryBuffer::Operation
{
  SetBufferSizeOperation(MemoryBuffer* o, const BufferSubresourceRange& r, uint32_t ac);
  bool perform(const RenderContext& renderContext, MemoryBuffer::MemoryBufferInternal& internals, std::shared_ptr<CommandBuffer> commandBuffer) override;
};

template<typename T>
struct SetDataOperation : public MemoryBuffer::Operation
{
  SetDataOperation(MemoryBuffer* o, const BufferSubresourceRange& r, const BufferSubresourceRange& sr, std::shared_ptr<T> data, uint32_t ac);
  bool perform(const RenderContext& renderContext, MemoryBuffer::MemoryBufferInternal& internals, std::shared_ptr<CommandBuffer> commandBuffer) override;
  void releaseResources(const RenderContext& renderContext) override;

  std::shared_ptr<T>                          data;
  BufferSubresourceRange                      sourceRange;
  std::vector<std::shared_ptr<StagingBuffer>> stagingBuffers;
};

const PerObjectBehaviour&              MemoryBuffer::getPerObjectBehaviour() const      { return perObjectBehaviour; }
const SwapChainImageBehaviour&         MemoryBuffer::getSwapChainImageBehaviour() const { return swapChainImageBehaviour; }
std::shared_ptr<DeviceMemoryAllocator> MemoryBuffer::getAllocator() const               { return allocator; }
VkBufferUsageFlags                     MemoryBuffer::getBufferUsage() const             { return bufferUsage; }


template <typename T>
Buffer<T>::Buffer(std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlags bufferUsage, PerObjectBehaviour perObjectBehaviour, SwapChainImageBehaviour swapChainImageBehaviour, bool useSetDataMethods)
  : MemoryBuffer{ allocator, bufferUsage, perObjectBehaviour, swapChainImageBehaviour, false, useSetDataMethods }
{
}

template <typename T>
Buffer<T>::Buffer(std::shared_ptr<T> d, std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlags bufferUsage, PerObjectBehaviour perObjectBehaviour, SwapChainImageBehaviour swapChainImageBehaviour)
  : MemoryBuffer{ allocator, bufferUsage, perObjectBehaviour, swapChainImageBehaviour, true, true }, data{ d }
{
}

template <typename T>
Buffer<T>::~Buffer()
{
}

template <typename T>
void Buffer<T>::setBufferSize(size_t bufferSize)
{
}

template <typename T>
void Buffer<T>::setBufferSize(Surface* surface, size_t bufferSize)
{
  CHECK_LOG_THROW(sameDataPerObject, "Cannot set buffer size per surface - data on all surfaces was declared as the same");
  std::lock_guard<std::mutex> lock(mutex);
  internalSetBufferSize(surface->getID(), surface->device.lock()->device, surface->surface, bufferSize);
}

template <typename T>
void Buffer<T>::setBufferSize(Device* device, size_t bufferSize)
{
  CHECK_LOG_THROW(sameDataPerObject, "Cannot set buffer size per device - data on all surfaces was declared as the same");
  std::lock_guard<std::mutex> lock(mutex);
  internalSetBufferSize(surface->getID(), surface->device.lock()->device, surface->surface, bufferSize);
}

template <typename T>
void Buffer<T>::invalidateData()
{
  CHECK_LOG_THROW(!sameDataPerObject, "Cannot invalidate data - wrong constructor used to create an object");
  std::lock_guard<std::mutex> lock(mutex);
  BufferSubresourceRange range(0,getDataSize());
  for (auto& pdd : perObjectData)
  {
    // remove all previous calls to setData
    pdd.second.commonData.bufferOperations.remove_if([](std::shared_ptr<Operation> bufop) { return bufop->type == MemoryBuffer::Operation::SetData; });
    // add setData operation with full texture size
    pdd.second.commonData.bufferOperations.push_back(std::make_shared<SetDataOperation<T>>(this, range, range, data, activeCount));
    pdd.second.invalidate();
  }
  invalidateResources();
}

template <typename T>
void Buffer<T>::setData(const T& dt)
{
  CHECK_LOG_THROW(!sameDataPerObject, "Cannot set data - wrong constructor used to create an object");
  *data = dt;
  invalidateData();
}

template <typename T>
void Buffer<T>::setData(Surface* surface, std::shared_ptr<T> dt)
{
  CHECK_LOG_THROW(sameDataPerObject, "Cannot set data per surface - data on all surfaces was declared as the same");
  CHECK_LOG_THROW(perObjectBehaviour != pbPerSurface, "Cannot set data per surface for this buffer");
  CHECK_LOG_THROW((bufferUsage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0, "Cannot set data for this buffer - user declared it as not writeable");
  std::lock_guard<std::mutex> lock(mutex);
  internalSetData(surface->getID(), surface->device.lock()->device, surface->surface, dt);
}

template <typename T>
void Buffer<T>::setData(Device* device, std::shared_ptr<T> dt)
{
  CHECK_LOG_THROW(sameDataPerObject, "Cannot set data per surface - data on all surfaces was declared as the same");
  CHECK_LOG_THROW(perObjectBehaviour != pbPerSurface, "Cannot set data per surface for this buffer");
  CHECK_LOG_THROW((bufferUsage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0, "Cannot set data for this buffer - user declared it as not writeable");
  internalSetData(device->getID(), device->device, VK_NULL_HANDLE, dt);
}

template <typename T>
void Buffer<T>::setData(Surface* surface, const T& dt)
{
  setData(surface, std::make_shared<T>(dt));
}

template <typename T>
void Buffer<T>::setData(Device* device, const T& dt)
{
  setData(device, std::make_shared<T>(dt));
}

template <typename T>
void*  Buffer<T>::getDataPointer()
{
  return uglyGetPointer(*data);
}

template <typename T>
size_t Buffer<T>::getDataSize()
{
  return uglyGetSize(*data);
}

template <typename T>
void Buffer<T>::sendDataToBuffer(uint32_t key, VkDevice device, VkSurfaceKHR surface)
{
  if(data!=nullptr)
    internalSetData(key, device, surface, data);
}

template <typename T>
void Buffer<T>::internalSetBufferSize(uint32_t key, VkDevice device, VkSurfaceKHR surface, size_t bufferSize)
{
  auto pddit = perObjectData.find(key);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ key, MemoryBuffer::MemoryBufferData(device, surface, activeCount, swapChainImageBehaviour) }).first;

  BufferSubresourceRange range(0, bufferSize);
  // remove all previous calls to setImage, but only when these calls are a subset of current call
  pddit->second.commonData.bufferOperations.remove_if([&range](std::shared_ptr<Operation> bufop) { return bufop->type == MemoryBuffer::Operation::SetBufferSize; });
  // add setImage operation
  pddit->second.commonData.bufferOperations.push_back(std::make_shared<SetBufferSizeOperation<T>>(this, range, activeCount));
  pddit->second.invalidate();
  invalidateResources();
}

template <typename T>
void Buffer<T>::internalSetData(uint32_t key, VkDevice device, VkSurfaceKHR surface, std::shared_ptr<T> dt)
{
  auto pddit = perObjectData.find(key);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ key, MemoryBuffer::MemoryBufferData(device, surface, activeCount, swapChainImageBehaviour) }).first;

  BufferSubresourceRange range(0, uglyGetSize(*dt));
  // remove all previous calls to setImage, but only when these calls are a subset of current call
  pddit->second.commonData.bufferOperations.remove_if([&range](std::shared_ptr<Operation> bufop) { return bufop->type == MemoryBuffer::Operation::SetData && range.contains(bufop->bufferRange); });
  // add setData operation
  pddit->second.commonData.bufferOperations.push_back(std::make_shared<SetDataOperation<T>>(this, range, range, dt, activeCount));
  pddit->second.invalidate();
  invalidateResources();
}

template<typename T>
SetBufferSizeOperation<T>::SetBufferSizeOperation(MemoryBuffer* o, const BufferSubresourceRange& r, uint32_t ac)
  : MemoryBuffer::Operation(o, MemoryBuffer::Operation::SetBufferSize, r, ac)
{
}

template<typename T>
bool SetBufferSizeOperation<T>::perform(const RenderContext& renderContext, MemoryBuffer::MemoryBufferInternal& internals, std::shared_ptr<CommandBuffer> commandBuffer)
{
  // release old buffer if exists
  auto ownerAllocator = owner->getAllocator();
  if (internals.buffer != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(renderContext.vkDevice, internals.buffer, nullptr);
    ownerAllocator->deallocate(renderContext.vkDevice, internals.memoryBlock);
    internals.buffer = VK_NULL_HANDLE;
    internals.memoryBlock = DeviceMemoryBlock();
  }
  VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = owner->getBufferUsage();
    bufferCreateInfo.size  = std::max<VkDeviceSize>(1, bufferRange.range);
  VK_CHECK_LOG_THROW(vkCreateBuffer(renderContext.vkDevice, &bufferCreateInfo, nullptr, &internals.buffer), "Cannot create a buffer");
  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(renderContext.vkDevice, internals.buffer, &memReqs);
  internals.memoryBlock = ownerAllocator->allocate(renderContext.device, memReqs);
  CHECK_LOG_THROW(internals.memoryBlock.alignedSize == 0, "Cannot create a bufer");
  ownerAllocator->bindBufferMemory(renderContext.device, internals.buffer, internals.memoryBlock.alignedOffset);

  owner->notifyCommandBufferSources(renderContext);
  owner->notifyBufferViews(renderContext, bufferRange);
  owner->notifyResources(renderContext);
  return false;
}

template<typename T>
SetDataOperation<T>::SetDataOperation(MemoryBuffer* o, const BufferSubresourceRange& r, const BufferSubresourceRange& sr, std::shared_ptr<T> d, uint32_t ac)
  : MemoryBuffer::Operation(o, MemoryBuffer::Operation::SetData, r, ac), sourceRange{ sr }, data{ d }
{
}

template<typename T>
bool SetDataOperation<T>::perform(const RenderContext& renderContext, MemoryBuffer::MemoryBufferInternal& internals, std::shared_ptr<CommandBuffer> commandBuffer)
{
  // if new data size is bigger than existing buffer size - we have to remove it
  auto ownerAllocator = owner->getAllocator();
  if (internals.buffer!=VK_NULL_HANDLE && internals.memoryBlock.alignedSize < uglyGetSize(*data))
  {
    vkDestroyBuffer(renderContext.vkDevice, internals.buffer, nullptr);
    ownerAllocator->deallocate(renderContext.vkDevice, internals.memoryBlock);
    internals.buffer      = VK_NULL_HANDLE;
    internals.memoryBlock = DeviceMemoryBlock();
  }

  if (internals.buffer == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
      bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferCreateInfo.usage = owner->getBufferUsage();
      bufferCreateInfo.size  = std::max<VkDeviceSize>(1, uglyGetSize(*data));
    VK_CHECK_LOG_THROW(vkCreateBuffer(renderContext.vkDevice, &bufferCreateInfo, nullptr, &internals.buffer), "Cannot create a buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(renderContext.vkDevice, internals.buffer, &memReqs);
    internals.memoryBlock = ownerAllocator->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(internals.memoryBlock.alignedSize == 0, "Cannot create a buffer");
    ownerAllocator->bindBufferMemory(renderContext.device, internals.buffer, internals.memoryBlock.alignedOffset);

    owner->notifyCommandBufferSources(renderContext);
    owner->notifyBufferViews(renderContext, bufferRange);
    owner->notifyResources(renderContext);
  }
  bool memoryIsLocal = ((ownerAllocator->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (uglyGetSize(*data) > 0)
  {
    if (memoryIsLocal)
    {
      std::shared_ptr<StagingBuffer> stagingBuffer = renderContext.device->acquireStagingBuffer(uglyGetPointer(*data), uglyGetSize(*data));
      VkBufferCopy copyRegion{};
      copyRegion.size = uglyGetSize(*data);
      commandBuffer->cmdCopyBuffer(stagingBuffer->buffer, internals.buffer, copyRegion);
      stagingBuffers.push_back(stagingBuffer);
    }
    else
    {
      ownerAllocator->copyToDeviceMemory(renderContext.device, internals.memoryBlock.alignedOffset, uglyGetPointer(*data), uglyGetSize(*data), 0);
    }
  }

  // if we sent some data and memory is not accessible from host ( is local ) - we generated no commands to command buffer
  return uglyGetSize(*data) > 0 && memoryIsLocal;
}

template<typename T>
void SetDataOperation<T>::releaseResources(const RenderContext& renderContext)
{
  for (auto& s : stagingBuffers)
    renderContext.device->releaseStagingBuffer(s);
  stagingBuffers.clear();
}

}