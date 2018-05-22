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

#include <pumex/MemoryBuffer.h>
#include <pumex/Surface.h>
#include <pumex/Command.h>
#include <pumex/RenderContext.h>
#include <pumex/Resource.h>
#include <algorithm>

using namespace pumex;

BufferSubresourceRange::BufferSubresourceRange(VkDeviceSize o, VkDeviceSize r)
  : offset{ o }, range{ r }
{
}

bool BufferSubresourceRange::contains(const BufferSubresourceRange& subRange) const
{
  return (offset <= subRange.offset) && (offset + range >= subRange.offset + subRange.range);
}

MemoryBuffer::MemoryBuffer(std::shared_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlags bu, PerObjectBehaviour pob, SwapChainImageBehaviour scib, bool sdpo, bool usdm)
  : perObjectBehaviour{ pob }, swapChainImageBehaviour{ scib }, sameDataPerObject{ sdpo }, allocator{ a }, bufferUsage{ bu }, activeCount{ 1 }
{
  if (usdm)
    bufferUsage = bufferUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
}

MemoryBuffer::~MemoryBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
  {
    for (uint32_t i = 0; i < pdd.second.data.size(); ++i)
    {
      vkDestroyBuffer(pdd.second.device, pdd.second.data[i].buffer, nullptr);
      allocator->deallocate(pdd.second.device, pdd.second.data[i].memoryBlock);
    }
  }
}

VkBuffer MemoryBuffer::getHandleBuffer(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(getKeyID(renderContext, perObjectBehaviour));
  if (pddit == end(perObjectData))
    return VK_NULL_HANDLE;
  return pddit->second.data[renderContext.activeIndex % activeCount].buffer;
}

size_t MemoryBuffer::getDataSize(const RenderContext& renderContext) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perObjectData.find(getKeyID(renderContext, perObjectBehaviour));
  if (pddit == end(perObjectData))
    return 0;
  return pddit->second.data[renderContext.activeIndex % activeCount].dataSize;
}

void MemoryBuffer::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (swapChainImageBehaviour == swForEachImage && renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
    {
      pdd.second.resize(activeCount);
      for (auto& op : pdd.second.commonData.bufferOperations)
        op->resize(activeCount);
    }
  }
  auto keyValue = getKeyID(renderContext, perObjectBehaviour);
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, MemoryBufferData(renderContext, swapChainImageBehaviour) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  // methods working per device may add PerObjectData without defining surface handle - we have to fill that gap
  if (pddit->second.surface == VK_NULL_HANDLE)
    pddit->second.surface = renderContext.vkSurface;

  // images are created here, when Texture uses sameTraitsPerObject - otherwise it's a reponsibility of the user to create them through setImageTraits() call
  if (pddit->second.data[activeIndex].buffer == nullptr && sameDataPerObject)
  {
    VkBufferCreateInfo bufferCreateInfo{};
      bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferCreateInfo.usage = bufferUsage;
      bufferCreateInfo.size  = std::max<VkDeviceSize>(1, getDataSize());
    VK_CHECK_LOG_THROW(vkCreateBuffer(pddit->second.device, &bufferCreateInfo, nullptr, &pddit->second.data[activeIndex].buffer), "Cannot create a buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(pddit->second.device, pddit->second.data[activeIndex].buffer, &memReqs);
    pddit->second.data[activeIndex].dataSize    = bufferCreateInfo.size;
    pddit->second.data[activeIndex].memoryBlock = allocator->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(pddit->second.data[activeIndex].memoryBlock.alignedSize == 0, "Cannot create a bufer");
    allocator->bindBufferMemory(renderContext.device, pddit->second.data[activeIndex].buffer, pddit->second.data[activeIndex].memoryBlock.alignedOffset);

    BufferSubresourceRange allBufferRange(0, getDataSize());
    notifyCommandBufferSources(renderContext);
    notifyBufferViews(renderContext, allBufferRange);
    notifyResources(renderContext);
    // if there's a data - it must be sent now
    sendDataToBuffer(keyValue, renderContext.vkDevice, renderContext.vkSurface);
  }
  // if there are some pending texture operations
  if (!pddit->second.commonData.bufferOperations.empty())
  {
    // perform all operations in a single command buffer
    auto cmdBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
    bool submit = false;
    for (auto& bufop : pddit->second.commonData.bufferOperations)
    {
      if (!bufop->updated[activeIndex])
      {
        submit |= bufop->perform(renderContext, pddit->second.data[activeIndex], cmdBuffer);
        // mark operation as done for this activeIndex
        bufop->updated[activeIndex] = true;
      }
    }
    renderContext.device->endSingleTimeCommands(cmdBuffer, renderContext.queue, submit);
    for (auto& bufop : pddit->second.commonData.bufferOperations)
      bufop->releaseResources(renderContext);
    // if all operations are done for each index - remove them from list
    pddit->second.commonData.bufferOperations.remove_if(([](std::shared_ptr<Operation> bufop) { return bufop->allUpdated(); }));
  }
  pddit->second.valid[activeIndex] = true;
}

void MemoryBuffer::addCommandBufferSource(std::shared_ptr<CommandBufferSource> cbSource)
{
  if (std::find_if(begin(commandBufferSources), end(commandBufferSources), [&cbSource](std::weak_ptr<CommandBufferSource> cbs) { return !cbs.expired() && cbs.lock().get() == cbSource.get(); }) == end(commandBufferSources))
    commandBufferSources.push_back(cbSource);
}

void MemoryBuffer::notifyCommandBufferSources(const RenderContext& renderContext)
{
  auto eit = std::remove_if(begin(commandBufferSources), end(commandBufferSources), [](std::weak_ptr<CommandBufferSource> r) { return r.expired();  });
  for (auto it = begin(commandBufferSources); it != eit; ++it)
    it->lock()->notifyCommandBuffers(renderContext.activeIndex);
  commandBufferSources.erase(eit, end(commandBufferSources));
}

void MemoryBuffer::addResource(std::shared_ptr<Resource> resource)
{
  if (std::find_if(begin(resources), end(resources), [&resource](std::weak_ptr<Resource> ia) { return !ia.expired() && ia.lock().get() == resource.get(); }) == end(resources))
    resources.push_back(resource);
}

void MemoryBuffer::invalidateResources()
{
  auto eit = std::remove_if(begin(resources), end(resources), [](std::weak_ptr<Resource> r) { return r.expired();  });
  for (auto it = begin(resources); it != eit; ++it)
    it->lock()->invalidateDescriptors();
  resources.erase(eit, end(resources));
}

void MemoryBuffer::notifyResources(const RenderContext& renderContext)
{
  auto eit = std::remove_if(begin(resources), end(resources), [](std::weak_ptr<Resource> r) { return r.expired();  });
  for (auto it = begin(resources); it != eit; ++it)
    it->lock()->notifyDescriptors(renderContext);
  resources.erase(eit, end(resources));
}

void MemoryBuffer::addBufferView(std::shared_ptr<BufferView> bufferView)
{
  if (std::find_if(begin(bufferViews), end(bufferViews), [&bufferView](std::weak_ptr<BufferView> bv) { return !bv.expired() && bv.lock().get() == bufferView.get(); }) == end(bufferViews))
    bufferViews.push_back(bufferView);
}

void MemoryBuffer::notifyBufferViews(const RenderContext& renderContext, const BufferSubresourceRange& range)
{
  auto eit = std::remove_if(begin(bufferViews), end(bufferViews), [](std::weak_ptr<BufferView> bv) { return bv.expired();  });
  for (auto it = begin(bufferViews); it != eit; ++it)
    if (range.contains(it->lock()->subresourceRange))
      it->lock()->notifyBufferView(renderContext);
  bufferViews.erase(eit, end(bufferViews));
}

BufferView::BufferView(std::shared_ptr<MemoryBuffer> b, const BufferSubresourceRange& r, VkFormat f)
  : std::enable_shared_from_this<BufferView>(), memBuffer{ b }, subresourceRange{ r }, format{ f }
{
}

BufferView::~BufferView()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perObjectData)
    for (uint32_t i = 0; i<pdd.second.data.size(); ++i)
      vkDestroyBufferView(pdd.second.device, pdd.second.data[i].bufferView, nullptr);
}

VkBuffer BufferView::getHandleBuffer(const RenderContext& renderContext) const
{
  return memBuffer->getHandleBuffer(renderContext);
}

VkBufferView BufferView::getBufferView(const RenderContext& renderContext) const
{
  auto keyValue = getKeyID(renderContext, memBuffer->getPerObjectBehaviour());
  auto pddit = perObjectData.find(keyValue);
  if (pddit == perObjectData.end())
    return VK_NULL_HANDLE;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  return pddit->second.data[activeIndex].bufferView;
}

void BufferView::validate(const RenderContext& renderContext)
{
  if (!registered)
  {
    memBuffer->addBufferView(shared_from_this());
    registered = true;
  }
  memBuffer->validate(renderContext);
  std::lock_guard<std::mutex> lock(mutex);
  if (memBuffer->getSwapChainImageBehaviour() == swForEachImage && renderContext.imageCount > activeCount)
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perObjectData)
      pdd.second.resize(activeCount);
  }
  auto keyValue = getKeyID(renderContext, memBuffer->getPerObjectBehaviour());
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, BufferViewData(renderContext, memBuffer->getSwapChainImageBehaviour()) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  if (pddit->second.data[activeIndex].bufferView != VK_NULL_HANDLE)
  {
    vkDestroyBufferView(pddit->second.device, pddit->second.data[activeIndex].bufferView, nullptr);
    pddit->second.data[activeIndex].bufferView = VK_NULL_HANDLE;
  }

  VkBufferViewCreateInfo bufferViewCI{};
    bufferViewCI.sType  = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    bufferViewCI.flags  = 0;
    bufferViewCI.buffer = getHandleBuffer(renderContext);
    bufferViewCI.format = format;
    bufferViewCI.offset = subresourceRange.offset;
    bufferViewCI.range  = subresourceRange.range;
  VK_CHECK_LOG_THROW(vkCreateBufferView(pddit->second.device, &bufferViewCI, nullptr, &pddit->second.data[activeIndex].bufferView), "failed vkCreateBufferView");

  notifyResources(renderContext);
  pddit->second.valid[activeIndex] = true;
}

void BufferView::notifyBufferView(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto keyValue = getKeyID(renderContext, memBuffer->getPerObjectBehaviour());
  auto pddit = perObjectData.find(keyValue);
  if (pddit == end(perObjectData))
    pddit = perObjectData.insert({ keyValue, BufferViewData(renderContext, memBuffer->getSwapChainImageBehaviour()) }).first;
  pddit->second.invalidate();
}

void BufferView::addResource(std::shared_ptr<Resource> resource)
{
  if (std::find_if(begin(resources), end(resources), [&resource](std::weak_ptr<Resource> r) { return !r.expired() && r.lock().get() == resource.get(); }) == end(resources))
    resources.push_back(resource);
}

void BufferView::notifyResources(const RenderContext& renderContext)
{
  auto eit = std::remove_if(begin(resources), end(resources), [](std::weak_ptr<Resource> r) { return r.expired();  });
  for (auto it = begin(resources); it != eit; ++it)
    it->lock()->notifyDescriptors(renderContext);
  resources.erase(eit, end(resources));
}
