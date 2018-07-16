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
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <glm/vec4.hpp>
#include <pumex/Export.h>

namespace pumex
{

class Surface;

class PUMEX_EXPORT TimeStatisticsChannel
{
public:
  TimeStatisticsChannel() = delete;
  explicit TimeStatisticsChannel(uint32_t valueCount, const std::wstring& channelName, const glm::vec4& color);

  void                             setValues(double valueBegin, double valueDuration);
  void                             getLastValues(double& outValueBegin, double& outValueDuration) const;
  void                             getLastValues(uint32_t count, std::vector<double>& outValueBegin, std::vector<double>& outValueDuration) const;

  inline std::wstring               getChannelName() const;
  inline glm::vec4                 getColor() const;
  inline double                    getAverageValue() const;
  inline double                    getMaxValue() const;
  inline double                    getMinValue() const;
  inline std::pair<double, double> getValue(unsigned long long frameNumber) const;
  void                             resetMinMax();

protected:
  std::wstring                           channelName;
  glm::vec4                              color;
  std::vector<std::pair<double, double>> values;   // start time and duration
  double                                 sumValue; // sum of all durations
  double                                 minValue;
  double                                 maxValue;
  uint32_t                               currentIndex;
};

class PUMEX_EXPORT TimeStatistics
{
public:
  TimeStatistics(uint32_t valueCount);

  void                                           registerGroup(uint32_t groupID, const std::wstring& groupName);
  void                                           unregisterGroup(uint32_t groupID);

  void                                           registerChannel(uint32_t channelID, uint32_t groupID, const std::wstring& channelName, const glm::vec4& color);
  void                                           unregisterChannel(uint32_t channelID);
  void                                           unregisterChannels(uint32_t groupID);
  inline void                                    setFlags(uint32_t flags);
  inline bool                                    hasFlags(uint32_t flag) const;

  inline const std::map<uint32_t, std::wstring>& getGroups() const;
  std::vector<uint32_t>                          getGroupChannelIDs(uint32_t groupID);
  const TimeStatisticsChannel&                   getChannel(uint32_t channelID);

  void                                          setValues(uint32_t channelID, double valueBegin, double valueDuration);
  void resetMinMaxValues();

protected:
  mutable std::mutex                  mutex;
  uint32_t                            flags;
  std::map<uint32_t, std::wstring>    groups;
  std::map<uint32_t, uint32_t>        groupChannelIndices;
  std::map<uint32_t, uint32_t>        channelIndices;
  std::vector<TimeStatisticsChannel>  channels;
  std::vector<uint32_t>               freeChannels;
  uint32_t                            valueCount;

};

std::wstring                            TimeStatisticsChannel::getChannelName() const                         { return channelName;}
glm::vec4                               TimeStatisticsChannel::getColor() const                               { return color; }
double                                  TimeStatisticsChannel::getAverageValue() const                        { return sumValue / values.size(); }
double                                  TimeStatisticsChannel::getMaxValue() const                            { return maxValue; }
double                                  TimeStatisticsChannel::getMinValue() const                            { return minValue; }
std::pair<double, double>               TimeStatisticsChannel::getValue(unsigned long long frameNumber) const { unsigned long long frame = frameNumber % values.size(); return values[frame]; }

void                                    TimeStatistics::setFlags(uint32_t f)                                  { flags = f; }
bool                                    TimeStatistics::hasFlags(uint32_t f) const                            { return (flags & f) == f; }
const std::map<uint32_t, std::wstring>& TimeStatistics::getGroups() const                                     { return groups; }

}