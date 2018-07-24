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

#include <pumex/StandardHandlers.h>
#include <iomanip>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/projection.hpp>
#include <pumex/Viewer.h>
#include <pumex/Surface.h>
#include <pumex/Descriptor.h>
#include <pumex/Pipeline.h>
#include <pumex/TimeStatistics.h>
#include <pumex/Text.h>
#include <pumex/MemoryBuffer.h>
#include <pumex/MemoryImage.h>
#include <pumex/Sampler.h>
#include <pumex/DrawVerticesNode.h>
#include <pumex/CombinedImageSampler.h>
#include <pumex/UniformBuffer.h>
#include <pumex/Camera.h>
#include <pumex/utils/Shapes.h>

using namespace pumex;

TimeStatisticsHandler::TimeStatisticsHandler(std::shared_ptr<Viewer> viewer, std::shared_ptr<PipelineCache> pipelineCache, std::shared_ptr<DeviceMemoryAllocator> buffersAllocator, std::shared_ptr<DeviceMemoryAllocator> texturesAllocator, std::shared_ptr<MemoryBuffer> textCameraBuffer, VkSampleCountFlagBits rasterizationSamples )
{
  showfFPS                   = { false, true, true };
  viewerStatisticsToCollect  = { 0,  TSV_STAT_RENDER, TSV_STAT_UPDATE | TSV_STAT_RENDER | TSV_STAT_RENDER_EVENTS };
  surfaceStatisticsToCollect = { 0,  0,              TSS_STAT_BASIC | TSS_STAT_BUFFERS | TSS_STAT_EVENTS };
  viewerStatisticsGroups     = { {}, {}, { TSV_GROUP_UPDATE, TSV_GROUP_RENDER, TSV_GROUP_RENDER_EVENTS} };
  surfaceStatisticsGroups    = { {}, {}, { TSS_GROUP_BASIC, TSS_GROUP_EVENTS, TSS_GROUP_SECONDARY_BUFFERS, TSS_GROUP_PRIMARY_BUFFERS, TSS_GROUP_PRIMARY_BUFFERS+1, TSS_GROUP_PRIMARY_BUFFERS+2, TSS_GROUP_PRIMARY_BUFFERS+3 } };
  
  // creating root node for statistics rendering
  statisticsRoot = std::make_shared<Group>();
  statisticsRoot->setName("statisticsRoot");

  drawSemantic = { { VertexSemantic::Position, 3 }, { VertexSemantic::Color, 4 } };

  // preparing pipeline for statistics rendering : draw pipeline
  std::vector<DescriptorSetLayoutBinding> drawLayoutBindings =
  {
    { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
  };
  auto drawDescriptorSetLayout = std::make_shared<DescriptorSetLayout>(drawLayoutBindings);
  drawDescriptorSetLayout->setPreferredPoolSize(18);

  // building pipeline layout
  auto drawPipelineLayout = std::make_shared<PipelineLayout>();
  drawPipelineLayout->descriptorSetLayouts.push_back(drawDescriptorSetLayout);
  auto drawPipeline = std::make_shared<GraphicsPipeline>(pipelineCache, drawPipelineLayout);
  drawPipeline->setName("drawPipeline");
  drawPipeline->vertexInput =
  {
    { 0, VK_VERTEX_INPUT_RATE_VERTEX, drawSemantic }
  };
  drawPipeline->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  drawPipeline->rasterizationSamples = rasterizationSamples;
  drawPipeline->blendAttachments =
  {
    { VK_TRUE, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
    VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD }
  };
  drawPipeline->depthTestEnable = VK_FALSE;
  drawPipeline->depthWriteEnable = VK_FALSE;
  drawPipeline->shaderStages =
  {
    { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<ShaderModule>(viewer, "shaders/stat_draw.vert.spv"), "main" },
    { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<ShaderModule>(viewer, "shaders/stat_draw.frag.spv"), "main" }
  };
  statisticsRoot->addChild(drawPipeline);

  // preparing node for quad drawing
  drawNode = std::make_shared<DrawVerticesNode>(drawSemantic, 0, buffersAllocator, pbPerSurface, swForEachImage, true );
  drawNode->setName("drawNode");
  drawPipeline->addChild(drawNode);

  drawCameraBuffer       = std::make_shared<Buffer<Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pbPerSurface, swOnce, true);
  auto drawCameraUbo     = std::make_shared<UniformBuffer>(drawCameraBuffer);
  auto drawDescriptorSet = std::make_shared<DescriptorSet>(drawDescriptorSetLayout);
  drawDescriptorSet->setDescriptor(0, drawCameraUbo);
  drawNode->setDescriptorSet(0, drawDescriptorSet);

  // preparing pipeline for statistics rendering : text pipeline
  // preparing two fonts and text nodes
  fontDefault = std::make_shared<Font>(viewer, "fonts/DejaVuSans.ttf", glm::uvec2(1024, 1024), 24, texturesAllocator);
  textDefault = std::make_shared<Text>(fontDefault, buffersAllocator);
  textDefault->setName("textDefault");

  std::vector<DescriptorSetLayoutBinding> textLayoutBindings =
  {
    { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_GEOMETRY_BIT },
    { 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
  };
  auto textDescriptorSetLayout = std::make_shared<DescriptorSetLayout>(textLayoutBindings);
  textDescriptorSetLayout->setPreferredPoolSize(18);
  // building pipeline layout
  auto textPipelineLayout = std::make_shared<PipelineLayout>();
  textPipelineLayout->descriptorSetLayouts.push_back(textDescriptorSetLayout);
  auto textPipeline = std::make_shared<GraphicsPipeline>(pipelineCache, textPipelineLayout);
  textPipeline->setName("textPipeline");
  textPipeline->vertexInput =
  {
    { 0, VK_VERTEX_INPUT_RATE_VERTEX, textDefault->textVertexSemantic }
  };
  textPipeline->topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  textPipeline->rasterizationSamples = rasterizationSamples;
  textPipeline->blendAttachments =
  {
    { VK_TRUE, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
      VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD }
  };
  textPipeline->depthTestEnable = VK_FALSE;
  textPipeline->depthWriteEnable = VK_FALSE;
  textPipeline->shaderStages =
  {
    { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<ShaderModule>(viewer, "shaders/text_draw.vert.spv"), "main" },
    { VK_SHADER_STAGE_GEOMETRY_BIT, std::make_shared<ShaderModule>(viewer, "shaders/text_draw.geom.spv"), "main" },
    { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<ShaderModule>(viewer, "shaders/text_draw.frag.spv"), "main" }
  };
  statisticsRoot->addChild(textPipeline);

  auto fontSampler = std::make_shared<Sampler>(SamplerTraits());

  // preparing two fonts and text nodes
  textPipeline->addChild(textDefault);

  auto textCameraUbo            = std::make_shared<UniformBuffer>(textCameraBuffer);
  auto fontDefaultImageView     = std::make_shared<ImageView>(fontDefault->fontMemoryImage, fontDefault->fontMemoryImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D);
  auto textDefaultDescriptorSet = std::make_shared<DescriptorSet>(textDescriptorSetLayout);
  textDefaultDescriptorSet->setDescriptor(0, textCameraUbo);
  textDefaultDescriptorSet->setDescriptor(1, std::make_shared<CombinedImageSampler>(fontDefaultImageView, fontSampler));
  textDefault->setDescriptorSet(0, textDefaultDescriptorSet);

  fontSmall = std::make_shared<Font>(viewer, "fonts/DejaVuSans.ttf", glm::uvec2(1024, 1024), 12, texturesAllocator);
  textSmall = std::make_shared<Text>(fontSmall, buffersAllocator);
  textSmall->setName("textSmall");
  textPipeline->addChild(textSmall);

  auto fontSmallImageView = std::make_shared<ImageView>(fontSmall->fontMemoryImage, fontSmall->fontMemoryImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D);
  auto textSmallDescriptorSet = std::make_shared<DescriptorSet>(textDescriptorSetLayout);
  textSmallDescriptorSet->setDescriptor(0, textCameraUbo);
    textSmallDescriptorSet->setDescriptor(1, std::make_shared<CombinedImageSampler>(fontSmallImageView, fontSampler));
  textSmall->setDescriptorSet(0, textSmallDescriptorSet);
}

void TimeStatisticsHandler::addChannelData(float minVal, uint32_t vertexSize, float h0, float h1, VertexAccumulator& acc, const TimeStatisticsChannel& channel, std::vector<float>& vertices, std::vector<uint32_t>& indices)
{
  std::vector<double> start, duration;
  channel.getLastValues(framesCount, start, duration);

  glm::vec4 color = channel.getColor();
  acc.set(VertexSemantic::Color, color.r, color.g, color.b, color.a);

  for (uint32_t i = 0; i < start.size(); ++i)
  {
    uint32_t verticesSoFar = vertices.size() / vertexSize;

    float fstarti = static_cast<float>(start[i]) - minVal;
    float fduri   = static_cast<float>(duration[i]);

    acc.set(VertexSemantic::Position, fstarti, h1);
    vertices.insert(end(vertices), cbegin(acc.values), cend(acc.values));

    acc.set(VertexSemantic::Position, fstarti, h0);
    vertices.insert(end(vertices), cbegin(acc.values), cend(acc.values));

    acc.set(VertexSemantic::Position, fstarti + fduri, h0);
    vertices.insert(end(vertices), cbegin(acc.values), cend(acc.values));

    acc.set(VertexSemantic::Position, fstarti + fduri, h1);
    vertices.insert(end(vertices), cbegin(acc.values), cend(acc.values));

    indices.push_back(verticesSoFar + 0);
    indices.push_back(verticesSoFar + 1);
    indices.push_back(verticesSoFar + 2);

    indices.push_back(verticesSoFar + 2);
    indices.push_back(verticesSoFar + 3);
    indices.push_back(verticesSoFar + 0);
  }
}

bool TimeStatisticsHandler::handle(const InputEvent& iEvent, Viewer* viewer)
{
  bool handled = false;
  switch (iEvent.type)
  {
  case pumex::InputEvent::KEYBOARD_KEY_PRESSED:
    switch (iEvent.key)
    {
    case pumex::InputEvent::F4:
      statisticsCollection = ( statisticsCollection + 1 ) % 3;
      handled = true;
      break;
    case pumex::InputEvent::F5:
      windowTime *= 2.0f;
      handled = true;
      break;
    case pumex::InputEvent::F6:
      windowTime /= 2.0f;
      handled = true;
      break;
    case pumex::InputEvent::F7:
      if(framesCount>1)
        framesCount--;
      handled = true;
      break;
    case pumex::InputEvent::F8:
      if (framesCount<31)
        framesCount++;
      handled = true;
      break;
    default: break;
    }
    break;
  }
  return handled;
}

void TimeStatisticsHandler::collectData(Surface* surface, TimeStatistics* viewerStatistics, TimeStatistics* surfaceStatistics)
{
  const uint32_t TSH_FPS_ID = 1;
  if (showfFPS[statisticsCollection])
  {
    const auto& fpsChannel = viewerStatistics->getChannel(TSV_CHANNEL_FRAME);

    auto averageFrameTime = fpsChannel.getAverageValue();
    double fpsValue = 1.0 / averageFrameTime;
    std::wstringstream stream;
    stream << "FPS : " << std::fixed << std::setprecision(1) << fpsValue;
    textDefault->setText(surface, TSH_FPS_ID, glm::vec2(40, 40), glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), stream.str());
  }
  else
  {
    // FIXME - I will have to work this out
    textDefault->clearTexts();
    textSmall->clearTexts();
  }
  std::vector<double> renderBegin, renderDuration;
  const auto& renderChannel = viewerStatistics->getChannel(TSV_CHANNEL_RENDER);
  renderChannel.getLastValues(framesCount, renderBegin, renderDuration);
  float minTime = renderBegin[0];

  // We want to have 0.0 time render at 130 pixels and windowTime render at renderWidth pixels
  float renderWidth  = surface->swapChainSize.width;
  float renderHeight = surface->swapChainSize.height;
  float minProjTime  = -130.0f * windowTime / (renderWidth - 130.0f);

  Camera drawCamera;
  drawCamera.setProjectionMatrix(glm::ortho(minProjTime, windowTime, 0.0f, renderHeight), false);
  drawCameraBuffer->setData(surface, drawCamera);

  uint32_t vertexSize = calcVertexSize(drawSemantic);
  VertexAccumulator acc(drawSemantic);

  std::vector<float> vertices;
  std::vector<uint32_t> indices;

  float channelHeight  = 80.0f;
  float dHeight = 40.0f;
  for (const auto& group : viewerStatistics->getGroups())
  {
    if (std::find(begin(viewerStatisticsGroups[statisticsCollection]), end(viewerStatisticsGroups[statisticsCollection]), group.first) == end(viewerStatisticsGroups[statisticsCollection]))
      continue;
    textSmall->setText(surface, 100 + group.first, glm::vec2(5, channelHeight -0.2*dHeight), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), group.second);
    auto channelIDs = viewerStatistics->getGroupChannelIDs(group.first);
    for (auto channelID : channelIDs)
    {
      if (channelID == TSV_CHANNEL_FRAME)
        continue;
      const auto& channel = viewerStatistics->getChannel(channelID);
      addChannelData(minTime, vertexSize, channelHeight, channelHeight - 0.8f*dHeight, acc, channel, vertices, indices);
    }
    channelHeight += dHeight;
  }
  viewerStatistics->resetMinMaxValues();
  viewerStatistics->setFlags(viewerStatisticsToCollect[statisticsCollection]);


  for (const auto& group : surfaceStatistics->getGroups())
  { 
    if (std::find(begin(surfaceStatisticsGroups[statisticsCollection]), end(surfaceStatisticsGroups[statisticsCollection]), group.first) == end(surfaceStatisticsGroups[statisticsCollection]))
      continue;
    textSmall->setText(surface, 200 + group.first, glm::vec2(5, channelHeight -0.2*dHeight), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), group.second);
    auto channelIDs = surfaceStatistics->getGroupChannelIDs(group.first);
    for (auto channelID : channelIDs)
    {
      const auto& channel = surfaceStatistics->getChannel(channelID);
      addChannelData(minTime, vertexSize, channelHeight, channelHeight - 0.8f*dHeight, acc, channel, vertices, indices);
    }
    channelHeight += dHeight;
  }
  drawNode->setVertexIndexData(surface, vertices, indices);
  surfaceStatistics->resetMinMaxValues();
  surfaceStatistics->setFlags(surfaceStatisticsToCollect[statisticsCollection]);
}

BasicCameraHandler::BasicCameraHandler()
{
  for(uint32_t i = 0; i < 3; ++i)
  {
    cameraCenter[i].position    = glm::vec3(0.0f, 0.0f, 1.0f);
    cameraCenter[i].orientation = glm::quat(glm::vec3(glm::half_pi<float>(), 0.0f, glm::half_pi<float>()));
    cameraDistance[i]           = 5.0f;

    glm::vec3 forwardVec(0.0f,0.0f, cameraDistance[i]);
    cameraReal[i].position      = cameraCenter[i].position + cameraCenter[i].orientation * forwardVec;
    cameraReal[i].orientation   = cameraCenter[i].orientation;
  }
}

bool BasicCameraHandler::handle(const InputEvent& iEvent, Viewer* viewer)
{
  bool handled = false;
  currMousePos = lastMousePos;

  switch (iEvent.type)
  {
  case pumex::InputEvent::MOUSE_KEY_PRESSED:
    if (iEvent.mouseButton == pumex::InputEvent::LEFT)
      leftMouseKeyPressed = true;
    else if (iEvent.mouseButton == pumex::InputEvent::RIGHT)
      rightMouseKeyPressed = true;
    else break;
    handled        = true;
    currMousePos.x = iEvent.x;
    currMousePos.y = iEvent.y;
    lastMousePos   = currMousePos;
    break;
  case pumex::InputEvent::MOUSE_KEY_RELEASED:
    if (iEvent.mouseButton == pumex::InputEvent::LEFT)
      leftMouseKeyPressed = false;
    else if (iEvent.mouseButton == pumex::InputEvent::RIGHT)
      rightMouseKeyPressed = false;
    else break;
    handled = true;
    break;
  case pumex::InputEvent::MOUSE_MOVE:
    if (leftMouseKeyPressed || rightMouseKeyPressed)
    {
      currMousePos.x = iEvent.x;
      currMousePos.y = iEvent.y;
      handled        = true;
    }
    break;
  case pumex::InputEvent::KEYBOARD_KEY_PRESSED:
    switch (iEvent.key)
    {
    case pumex::InputEvent::W:     moveForward  = true; handled = true; break;
    case pumex::InputEvent::S:     moveBackward = true; handled = true; break;
    case pumex::InputEvent::A:     moveLeft     = true; handled = true; break;
    case pumex::InputEvent::D:     moveRight    = true; handled = true; break;
    case pumex::InputEvent::Q:     moveUp       = true; handled = true; break;
    case pumex::InputEvent::Z:     moveDown     = true; handled = true; break;
    case pumex::InputEvent::SHIFT: moveFast     = true; handled = true; break;
    default: break;
    }
    break;
  case pumex::InputEvent::KEYBOARD_KEY_RELEASED:
    switch (iEvent.key)
    {
    case pumex::InputEvent::W:     moveForward  = false; handled = true; break;
    case pumex::InputEvent::S:     moveBackward = false; handled = true; break;
    case pumex::InputEvent::A:     moveLeft     = false; handled = true; break;
    case pumex::InputEvent::D:     moveRight    = false; handled = true; break;
    case pumex::InputEvent::Q:     moveUp       = false; handled = true; break;
    case pumex::InputEvent::Z:     moveDown     = false; handled = true; break;
    case pumex::InputEvent::SHIFT: moveFast     = false; handled = true; break;
    default: break;
    }
    break;
  default:
    break;
  }
  if (!handled)
    return false;
  return true;
}

void BasicCameraHandler::update(Viewer* viewer)
{
  uint32_t updateIndex     = viewer->getUpdateIndex();
  uint32_t prevUpdateIndex = viewer->getPreviousUpdateIndex();

  cameraCenter[updateIndex]   = cameraCenter[prevUpdateIndex];
  cameraDistance[updateIndex] = cameraDistance[prevUpdateIndex];
  float deltaTime             = inSeconds(viewer->getUpdateDuration());

  if (leftMouseKeyPressed)
  {
    glm::quat qx = glm::angleAxis(5.0f * (lastMousePos.y - currMousePos.y), glm::vec3(1.0, 0.0, 0.0));
    glm::quat qz = glm::angleAxis(5.0f * (lastMousePos.x - currMousePos.x), glm::vec3(0.0, 0.0, 1.0));
    cameraCenter[updateIndex].orientation = glm::normalize( qz * cameraCenter[updateIndex].orientation * qx );
    lastMousePos = currMousePos;
  }

  if (rightMouseKeyPressed)
  {
    cameraDistance[updateIndex] += 10.0f*(lastMousePos.y - currMousePos.y);
    if (cameraDistance[updateIndex] < 0.1f)
      cameraDistance[updateIndex] = 0.1f;
    lastMousePos = currMousePos;
  }

  float camStep = 8.0f * deltaTime;
  if (moveFast)
    camStep = 24.0f * deltaTime;

  glm::vec3 camForward    = cameraCenter[updateIndex].orientation * glm::vec3(0.0f, 0.0f, 1.0f);
  glm::vec3 groundForward = glm::normalize(camForward - glm::proj(camForward, glm::vec3(0.0f, 0.0f, 1.0f)));
  glm::vec3 camRight      = cameraCenter[updateIndex].orientation * glm::vec3(1.0f, 0.0f, 0.0f);
  glm::vec3 groundRight   = glm::normalize(camRight - glm::proj(camRight, glm::vec3(0.0f, 0.0f, 1.0f)));
  glm::vec3 groundUp      = glm::vec3(0.0f, 0.0f, 1.0f);

  if (moveForward)
    cameraCenter[updateIndex].position -= groundForward * camStep;
  if (moveBackward)
    cameraCenter[updateIndex].position += groundForward * camStep;
  if (moveLeft)
    cameraCenter[updateIndex].position -= groundRight * camStep;
  if (moveRight)
    cameraCenter[updateIndex].position += groundRight * camStep;
  if (moveUp)
    cameraCenter[updateIndex].position += groundUp * camStep;
  if (moveDown)
    cameraCenter[updateIndex].position -= groundUp * camStep;

  calculateVelocitiesFromPositionOrientation(cameraCenter[updateIndex], cameraCenter[prevUpdateIndex], deltaTime);

  glm::vec3 distance3(0.0f, 0.0f, cameraDistance[updateIndex]);
  cameraReal[updateIndex].position    = cameraCenter[updateIndex].position + cameraCenter[updateIndex].orientation * distance3;
  cameraReal[updateIndex].orientation = cameraCenter[updateIndex].orientation;

  calculateVelocitiesFromPositionOrientation(cameraReal[updateIndex], cameraReal[prevUpdateIndex], deltaTime);
}

glm::mat4 BasicCameraHandler::getViewMatrix(Surface* surface)
{
  Viewer* viewer       = surface->viewer.lock().get();
  uint32_t renderIndex = viewer->getRenderIndex();
  float deltaTime      = inSeconds( viewer->getRenderTimeDelta() );
  return glm::inverse(extrapolate(cameraReal[renderIndex], deltaTime));
}

glm::vec4 BasicCameraHandler::getObserverPosition(Surface* surface)
{
  Viewer* viewer = surface->viewer.lock().get();
  uint32_t renderIndex = viewer->getRenderIndex();
  float deltaTime = inSeconds(viewer->getRenderTimeDelta());
  glm::vec3 position = cameraReal[renderIndex].position + cameraReal[renderIndex].velocity * deltaTime;
  return glm::vec4(position.x, position.y, position.z, 1.0f);
}
