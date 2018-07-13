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
#include <memory>
#include <pumex/Export.h>
#include <pumex/Node.h>

namespace pumex
{

class Viewer;
class DeviceMemoryAllocator;
class PipelineCache;
class Surface;
class TimeStatistics;
class Font;
class Text;
class DrawVerticesNode;
class MemoryBuffer;

class PUMEX_EXPORT TimeStatisticsHandler
{
public:
  TimeStatisticsHandler(std::shared_ptr<Viewer> viewer, std::shared_ptr<PipelineCache> pipelineCache, std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator, std::shared_ptr<pumex::DeviceMemoryAllocator> texturesAllocator);

  void collectData(Surface* surface, TimeStatistics* viewerStatistics, TimeStatistics* surfaceStatistics);

  void setTextCameraBuffer(std::shared_ptr<MemoryBuffer> memoryBuffer);

  inline std::shared_ptr<Group> getRoot() const;
protected:
  std::shared_ptr<Group>            statisticsRoot;
  std::shared_ptr<GraphicsPipeline> textPipeline;
  std::shared_ptr<GraphicsPipeline> drawPipeline;
  std::shared_ptr<DrawVerticesNode> drawNode;
  std::shared_ptr<Text>             textDefault;
  std::shared_ptr<Text>             textSmall;
  std::shared_ptr<Font>             fontDefault;
  std::shared_ptr<Font>             fontSmall;
};

inline std::shared_ptr<Group> TimeStatisticsHandler::getRoot() const
{
  return statisticsRoot;
}


}