#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

class Device;
class CommandBuffer;

// class holding Vulkan queries ( render time, rendered triangles count, etc )
class PUMEX_EXPORT QueryPool
{
public:
  explicit QueryPool(VkQueryType queryType, uint32_t poolSize, VkQueryPipelineStatisticFlags pipelineStatistics = 0);
  QueryPool(const QueryPool&)            = delete;
  QueryPool& operator=(const QueryPool&) = delete;
  virtual ~QueryPool();

  void validate(std::shared_ptr<pumex::Device> device);

  void reset(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> cmdBuffer, uint32_t firstQuery = 0, uint32_t queryCount = 0);
  void beginQuery(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> cmdBuffer, uint32_t query, VkQueryControlFlags controlFlags = 0);
  void endQuery(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> cmdBuffer, uint32_t query);
  void queryTimeStamp(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> cmdBuffer, uint32_t query, VkPipelineStageFlagBits pipelineStage);
  std::vector<uint64_t> getResults(std::shared_ptr<pumex::Device> device, uint32_t firstQuery = 0, uint32_t queryCount = 0, VkQueryResultFlags resultFlags = 0);
//  void copyResultsToBuffer(std::shared_ptr<pumex::Device> device, FIXME )

  VkQueryType                   queryType;
  uint32_t                      poolSize;
  VkQueryPipelineStatisticFlags pipelineStatistics;
protected:
  struct PerDeviceData
  {
    VkQueryPool queryPool = VK_NULL_HANDLE;
    bool        dirty     = true;
  };
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

	
}
