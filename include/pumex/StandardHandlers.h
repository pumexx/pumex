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
#include <array>
#include <vector>
#include <pumex/Export.h>
#include <pumex/Node.h>
#include <pumex/InputEvent.h>
#include <pumex/Kinematic.h>

namespace pumex
{

class  Viewer;
class  DeviceMemoryAllocator;
class  PipelineCache;
class  Surface;
class  TimeStatistics;
class  TimeStatisticsChannel;
class  VertexAccumulator;
class  Font;
class  Text;
class  DrawVerticesNode;
class  MemoryBuffer;
class  Camera;
struct VertexSemantic;
template <typename T> class Buffer;

class PUMEX_EXPORT TimeStatisticsHandler : public InputEventHandler
{
public:
  TimeStatisticsHandler(std::shared_ptr<Viewer> viewer, std::shared_ptr<PipelineCache> pipelineCache, std::shared_ptr<DeviceMemoryAllocator> buffersAllocator, std::shared_ptr<DeviceMemoryAllocator> texturesAllocator, std::shared_ptr<MemoryBuffer> textCameraBuffer, VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT);

  bool handle(const InputEvent& iEvent, Viewer* viewer) override;
  void collectData(Surface* surface, TimeStatistics* viewerStatistics, TimeStatistics* surfaceStatistics);

  inline std::shared_ptr<Group> getRoot() const;
protected:
  void addChannelData(float minVal, uint32_t vertexSize, float h0, float h1, VertexAccumulator& acc, const TimeStatisticsChannel& channel, std::vector<float>& vertices, std::vector<uint32_t>& indices);

  std::vector<VertexSemantic>        drawSemantic;

  std::shared_ptr<Group>             statisticsRoot;

  std::shared_ptr<GraphicsPipeline>  drawPipeline;
  std::shared_ptr<DrawVerticesNode>  drawNode;
  std::shared_ptr<pumex::Buffer<pumex::Camera>> drawCameraBuffer;

  std::shared_ptr<GraphicsPipeline>  textPipeline;
  std::shared_ptr<Text>              textDefault;
  std::shared_ptr<Text>              textSmall;
  std::shared_ptr<Font>              fontDefault;
  std::shared_ptr<Font>              fontSmall;

  uint32_t                           statisticsCollection       = 0;
  float                              windowTime                 = 0.01f;
  uint32_t                           framesCount                = 5;

  std::vector<char>                  showfFPS;
  std::vector<uint32_t>              viewerStatisticsToCollect;
  std::vector<uint32_t>              surfaceStatisticsToCollect;
  std::vector<std::vector<uint32_t>> viewerStatisticsGroups;
  std::vector<std::vector<uint32_t>> surfaceStatisticsGroups;
};

class PUMEX_EXPORT BasicCameraHandler : public InputEventHandler
{
public:
  BasicCameraHandler();

  bool handle(const InputEvent& iEvent, Viewer* viewer) override;
  void update(Viewer* viewer);
  glm::mat4 getViewMatrix(Surface* surface);
  glm::vec4 getObserverPosition(Surface* surface);

  void setCameraVelocity(float slow, float fast);

protected:

  std::array<Kinematic,3>  cameraCenter;
  std::array<float, 3>     cameraDistance;
  std::array<Kinematic, 3> cameraReal;

  glm::vec2 lastMousePos;
  glm::vec2 currMousePos;
  bool      leftMouseKeyPressed  = false;
  bool      rightMouseKeyPressed = false;

  bool      moveForward          = false;
  bool      moveBackward         = false;
  bool      moveLeft             = false;
  bool      moveRight            = false;
  bool      moveUp               = false;
  bool      moveDown             = false;
  bool      moveFast             = false;

  float     velocitySlow         = 8.0f;
  float     velocityFast         = 24.0f;
};

inline std::shared_ptr<Group> TimeStatisticsHandler::getRoot() const { return statisticsRoot; }

}