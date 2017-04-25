#include <pumex/Query.h>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/utils/Log.h>


using namespace pumex;

QueryPool::QueryPool(VkQueryType qt, uint32_t p, VkQueryPipelineStatisticFlags ps)
  : queryType(qt), poolSize(p), pipelineStatistics(ps)
{
}

QueryPool::~QueryPool()
{
  for (auto& pddit : perDeviceData)
    vkDestroyQueryPool(pddit.first, pddit.second.queryPool, nullptr);
}


void QueryPool::validate(std::shared_ptr<pumex::Device> device)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit != perDeviceData.end())
    return;
  pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;

  VkQueryPoolCreateInfo queryPoolCI{};
    queryPoolCI.sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolCI.queryType          = queryType;
    queryPoolCI.queryCount         = poolSize;
    queryPoolCI.pipelineStatistics = pipelineStatistics;
  VK_CHECK_LOG_THROW(vkCreateQueryPool(pddit->first, &queryPoolCI, nullptr, &pddit->second.queryPool), "Cannot create query pool");
}

void QueryPool::reset(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> cmdBuffer, uint32_t firstQuery, uint32_t queryCount)
{
  auto pddit = perDeviceData.find(device->device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Query pool was not validated before resetting");
  if (firstQuery == 0 && queryCount == 0)
    queryCount = poolSize;
  vkCmdResetQueryPool(cmdBuffer->getHandle(), pddit->second.queryPool, firstQuery, queryCount);
}

void QueryPool::beginQuery(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> cmdBuffer, uint32_t query, VkQueryControlFlags controlFlags)
{
  auto pddit = perDeviceData.find(device->device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Query pool was not validated before beginQuery");
  vkCmdBeginQuery(cmdBuffer->getHandle(), pddit->second.queryPool, query, controlFlags);
}

void QueryPool::endQuery(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> cmdBuffer, uint32_t query)
{
  auto pddit = perDeviceData.find(device->device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Query pool was not validated before endQuery");
  vkCmdEndQuery(cmdBuffer->getHandle(), pddit->second.queryPool, query);
}

void QueryPool::queryTimeStamp(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> cmdBuffer, uint32_t query, VkPipelineStageFlagBits pipelineStage)
{
  auto pddit = perDeviceData.find(device->device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Query pool was not validated before endQuery");
  vkCmdWriteTimestamp(cmdBuffer->getHandle(), pipelineStage, pddit->second.queryPool, query);
}

std::vector<uint64_t> QueryPool::getResults(std::shared_ptr<pumex::Device> device, uint32_t firstQuery, uint32_t queryCount, VkQueryResultFlags resultFlags)
{
  auto pddit = perDeviceData.find(device->device);
  CHECK_LOG_THROW(pddit == perDeviceData.end(), "Query pool was not validated before getting the results");

  if (firstQuery == 0 && queryCount == 0)
    queryCount = poolSize;
  std::vector<uint64_t> results(queryCount);
  //VK_CHECK_LOG_THROW( vkGetQueryPoolResults(pddit->first, pddit->second.queryPool, firstQuery, queryCount, sizeof(uint64_t) * results.size(), results.data(), sizeof(uint64_t), resultFlags & VK_QUERY_RESULT_64_BIT ), "Cannot query the results");
  vkGetQueryPoolResults(pddit->first, pddit->second.queryPool, firstQuery, queryCount, sizeof(uint64_t) * results.size(), results.data(), sizeof(uint64_t), resultFlags & VK_QUERY_RESULT_64_BIT);
  return results;
}
