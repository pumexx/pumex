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

#include <pumex/TimeStatistics.h>
#include <algorithm>
#include <pumex/utils/Log.h>

using namespace pumex;

TimeStatisticsChannel::TimeStatisticsChannel(uint32_t valueCount, const std::wstring& chn, const glm::vec4& c)
  : sumValue{ 0.0 }, currentIndex{ 0 }, channelName{ chn }, color{ c }
{
  CHECK_LOG_THROW(valueCount == 0, "Cannot make StatisticsChannel with value == 0");

  values.resize(valueCount, std::make_pair(0.0, 0.0) );
  resetMinMax();
}

void TimeStatisticsChannel::setValues(double valueBegin, double valueDuration)
{
  sumValue                    += valueDuration;
  sumValue                    -= values[currentIndex].second;
  values[currentIndex].first  = valueBegin;
  values[currentIndex].second = valueDuration;

  if (valueDuration < minValue) minValue = valueDuration;
  if (valueDuration > maxValue) maxValue = valueDuration;

  currentIndex = (currentIndex + 1) % static_cast<uint32_t>(values.size());
}

void TimeStatisticsChannel::getLastValues(double& outValueBegin, double& outValueDuration) const
{
  uint32_t valueCount = static_cast<uint32_t>(values.size());
  uint32_t targetIndex = (currentIndex + valueCount - 1 ) % valueCount;
  outValueBegin    = values[targetIndex].first;
  outValueDuration = values[targetIndex].second;
}

void TimeStatisticsChannel::getLastValues(uint32_t count, std::vector<double>& outValueBegin, std::vector<double>& outValueDuration) const
{
  uint32_t valueCount = static_cast<uint32_t>(values.size());
  CHECK_LOG_THROW(count > valueCount, "Value count is too big : " << count);
  uint32_t targetIndex = (currentIndex + valueCount - count) % valueCount;
  for (uint32_t i = 0; i < count; ++i)
  {
    outValueBegin.push_back(values[targetIndex].first);
    outValueDuration.push_back(values[targetIndex].second);
    targetIndex = (targetIndex + 1) % valueCount;
  }
}

void TimeStatisticsChannel::resetMinMax()
{
  minValue = std::numeric_limits<double>::max();
  maxValue = std::numeric_limits<double>::lowest();
}

TimeStatistics::TimeStatistics(uint32_t vc)
  : valueCount{ vc }, flags{ 0 }
{
}

void TimeStatistics::registerGroup(uint32_t groupID, const std::wstring& groupName)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto git = groups.find(groupID);
  CHECK_LOG_THROW(git != end(groups), "Statistics group already exists: " << groupID);
  groups.insert({ groupID, groupName });
}

void TimeStatistics::unregisterGroup(uint32_t groupID)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto git = groups.find(groupID);
  CHECK_LOG_THROW(git == end(groups), "Cannot unregister nonexisting group: " << groupID);
  groups.erase(git);
}

void TimeStatistics::registerChannel(uint32_t channelID, uint32_t groupID, const std::wstring& channelName, const glm::vec4& color)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = channelIndices.find(channelID);
  CHECK_LOG_THROW(it != end(channelIndices), "Statistics channel already exists: " << channelID );

  auto git = groups.find(groupID);
  CHECK_LOG_THROW(git == end(groups), "Statistics group is missing: " << groupID);

  if (!freeChannels.empty())
  {
    uint32_t newChannel = freeChannels.back();
    freeChannels.pop_back();
    channelIndices[channelID]      = newChannel;
    channels[newChannel]           = TimeStatisticsChannel(valueCount,channelName, color);
  }
  else
  {
    channelIndices[channelID] = channels.size();
    channels.push_back(TimeStatisticsChannel(valueCount, channelName, color));
  }
  groupChannelIndices[channelID] = groupID;
}

void TimeStatistics::unregisterChannel(uint32_t channelID)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = channelIndices.find(channelID);
  CHECK_LOG_THROW(it == end(channelIndices), "Cannot unregister statistics channel : " << channelID);
  freeChannels.push_back(it->second);
  channelIndices.erase(it);
  auto git = groupChannelIndices.find(channelID);
  CHECK_LOG_THROW(git == end(groupChannelIndices), "Cannot unregister statistics channel from group : " << channelID );
  groupChannelIndices.erase(git);
}

void TimeStatistics::unregisterChannels(uint32_t groupID)
{
  auto git = groups.find(groupID);
  CHECK_LOG_THROW(git == end(groups), "Statistics group is missing: " << groupID);

  std::vector<uint32_t> channelIDs = getGroupChannelIDs(groupID);
  for(auto channelID : channelIDs)
    unregisterChannel(channelID);
}

std::vector<uint32_t> TimeStatistics::getGroupChannelIDs(uint32_t groupID)
{
  std::lock_guard<std::mutex> lock(mutex);
  std::vector<uint32_t> result;
  for (const auto& it : groupChannelIndices)
  {
    if (it.second == groupID)
      result.push_back(it.first);
  }
  return std::move(result);
}

const TimeStatisticsChannel& TimeStatistics::getChannel(uint32_t channelID)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = channelIndices.find(channelID);
  CHECK_LOG_THROW(it == end(channelIndices), "Statistics channel does not exist : " << channelID);
  return channels[it->second];
}

void TimeStatistics::setValues(uint32_t channelID, double valueBegin, double valueDuration)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = channelIndices.find(channelID);
  CHECK_LOG_THROW(it == end(channelIndices), "Statistics channel does not exist : "<< channelID);
  channels[it->second].setValues(valueBegin, valueDuration);
}

void TimeStatistics::resetMinMaxValues()
{
  for (auto& channel : channels)
    channel.resetMinMax();
}
