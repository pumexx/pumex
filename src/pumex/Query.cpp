//
// Copyright(c) 2017-2018 Pawe³ Ksiê¿opolski ( pumexx )
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

#include <pumex/Query.h>
#include <pumex/Device.h>
#include <pumex/Surface.h>
#include <pumex/Command.h>
#include <pumex/utils/Log.h>


using namespace pumex;

QueryPool::QueryPool(VkQueryType qt, uint32_t p, VkQueryPipelineStatisticFlags ps)
  : queryType(qt), poolSize(p), pipelineStatistics(ps)
{
}

QueryPool::~QueryPool()
{
  for (auto& pddit : perSurfaceData)
    vkDestroyQueryPool(pddit.second.device, pddit.second.queryPool, nullptr);
}


void QueryPool::validate(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit != end(perSurfaceData))
    return;
  pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(surface->device.lock()->device) }).first;

  VkQueryPoolCreateInfo queryPoolCI{};
    queryPoolCI.sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolCI.queryType          = queryType;
    queryPoolCI.queryCount         = poolSize;
    queryPoolCI.pipelineStatistics = pipelineStatistics;
  VK_CHECK_LOG_THROW(vkCreateQueryPool(pddit->second.device, &queryPoolCI, nullptr, &pddit->second.queryPool), "Cannot create query pool");
}

void QueryPool::reset(Surface* surface, std::shared_ptr<CommandBuffer> cmdBuffer, uint32_t firstQuery, uint32_t queryCount)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  CHECK_LOG_THROW(pddit == end(perSurfaceData), "Query pool was not validated before resetting");
  if (firstQuery == 0 && queryCount == 0)
    queryCount = poolSize;
  vkCmdResetQueryPool(cmdBuffer->getHandle(), pddit->second.queryPool, firstQuery, queryCount);
}

void QueryPool::beginQuery(Surface* surface, std::shared_ptr<CommandBuffer> cmdBuffer, uint32_t query, VkQueryControlFlags controlFlags)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  CHECK_LOG_THROW(pddit == end(perSurfaceData), "Query pool was not validated before beginQuery");
  vkCmdBeginQuery(cmdBuffer->getHandle(), pddit->second.queryPool, query, controlFlags);
}

void QueryPool::endQuery(Surface* surface, std::shared_ptr<CommandBuffer> cmdBuffer, uint32_t query)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  CHECK_LOG_THROW(pddit == end(perSurfaceData), "Query pool was not validated before endQuery");
  vkCmdEndQuery(cmdBuffer->getHandle(), pddit->second.queryPool, query);
}

void QueryPool::queryTimeStamp(Surface* surface, std::shared_ptr<CommandBuffer> cmdBuffer, uint32_t query, VkPipelineStageFlagBits pipelineStage)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  CHECK_LOG_THROW(pddit == end(perSurfaceData), "Query pool was not validated before queryTimeStamp()");
  vkCmdWriteTimestamp(cmdBuffer->getHandle(), pipelineStage, pddit->second.queryPool, query);
}

std::vector<uint64_t> QueryPool::getResults(Surface* surface, uint32_t firstQuery, uint32_t queryCount, VkQueryResultFlags resultFlags)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  CHECK_LOG_THROW(pddit == end(perSurfaceData), "Query pool was not validated before getting the results");

  if (firstQuery == 0 && queryCount == 0)
    queryCount = poolSize;
  std::vector<uint64_t> results(queryCount);
  //VK_CHECK_LOG_THROW( vkGetQueryPoolResults(pddit->first, pddit->second.queryPool, firstQuery, queryCount, sizeof(uint64_t) * results.size(), results.data(), sizeof(uint64_t), resultFlags & VK_QUERY_RESULT_64_BIT ), "Cannot query the results");
  vkGetQueryPoolResults(pddit->second.device, pddit->second.queryPool, firstQuery, queryCount, sizeof(uint64_t) * results.size(), results.data(), sizeof(uint64_t), resultFlags & VK_QUERY_RESULT_64_BIT);
  return results;
}
