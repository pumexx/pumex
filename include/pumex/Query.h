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

#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

class Device;
class CommandBuffer;

// class storing Vulkan queries ( render time, rendered triangles count, etc )
class PUMEX_EXPORT QueryPool
{
public:
  QueryPool()                            = delete;
  explicit QueryPool(VkQueryType queryType, uint32_t poolSize, VkQueryPipelineStatisticFlags pipelineStatistics = 0);
  QueryPool(const QueryPool&)            = delete;
  QueryPool& operator=(const QueryPool&) = delete;
  virtual ~QueryPool();

  void validate(Device* device);

  void reset(Device* device, std::shared_ptr<CommandBuffer> cmdBuffer, uint32_t firstQuery = 0, uint32_t queryCount = 0);
  void beginQuery(Device* device, std::shared_ptr<CommandBuffer> cmdBuffer, uint32_t query, VkQueryControlFlags controlFlags = 0);
  void endQuery(Device* device, std::shared_ptr<CommandBuffer> cmdBuffer, uint32_t query);
  void queryTimeStamp(Device* device, std::shared_ptr<CommandBuffer> cmdBuffer, uint32_t query, VkPipelineStageFlagBits pipelineStage);
  std::vector<uint64_t> getResults(Device* device, uint32_t firstQuery = 0, uint32_t queryCount = 0, VkQueryResultFlags resultFlags = 0);

  VkQueryType                   queryType;
  uint32_t                      poolSize;
  VkQueryPipelineStatisticFlags pipelineStatistics;
protected:
  struct PerDeviceData
  {
    VkQueryPool queryPool = VK_NULL_HANDLE;
  };
  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

	
}
