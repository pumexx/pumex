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

using namespace pumex;

TimeStatisticsHandler::TimeStatisticsHandler(std::shared_ptr<Viewer> viewer, std::shared_ptr<PipelineCache> pipelineCache, std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator, std::shared_ptr<pumex::DeviceMemoryAllocator> texturesAllocator)
{
  // creating root node for statistics rendering
  statisticsRoot = std::make_shared<Group>();
  statisticsRoot->setName("statisticsRoot");

  //// preparing pipeline for statistics rendering : draw pipeline
  //std::vector<DescriptorSetLayoutBinding> drawLayoutBindings =
  //{
  //  { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_GEOMETRY_BIT }
  //};
  //auto drawDescriptorSetLayout = std::make_shared<DescriptorSetLayout>(drawLayoutBindings);
  //// building pipeline layout
  //auto drawPipelineLayout = std::make_shared<PipelineLayout>();
  //drawPipelineLayout->descriptorSetLayouts.push_back(drawDescriptorSetLayout);
  //auto drawPipeline = std::make_shared<GraphicsPipeline>(pipelineCache, drawPipelineLayout);
  //drawPipeline->setName("drawPipeline");
  //drawPipeline->vertexInput =
  //{
  //  { 0, VK_VERTEX_INPUT_RATE_VERTEX, textDefault->textVertexSemantic }
  //};
  //drawPipeline->topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  //drawPipeline->blendAttachments =
  //{
  //  { VK_TRUE, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  //  VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
  //  VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD }
  //};
  //drawPipeline->depthTestEnable = VK_FALSE;
  //drawPipeline->depthWriteEnable = VK_FALSE;
  //drawPipeline->shaderStages =
  //{
  //  { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewer, "shaders/stat_draw.vert.spv"), "main" },
  //  { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/stat_draw.frag.spv"), "main" }
  //};
  //statisticsRoot->addChild(drawPipeline);

  //// preparing node for quad drawing

  // preparing pipeline for statistics rendering : text pipeline
  // preparing two fonts and text nodes
  fontDefault = std::make_shared<pumex::Font>(viewer, "fonts/DejaVuSans.ttf", glm::uvec2(1024, 1024), 24, texturesAllocator);
  textDefault = std::make_shared<pumex::Text>(fontDefault, buffersAllocator);
  textDefault->setName("textDefault");


  std::vector<DescriptorSetLayoutBinding> textLayoutBindings =
  {
    { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_GEOMETRY_BIT },
    { 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
  };
  auto textDescriptorSetLayout = std::make_shared<DescriptorSetLayout>(textLayoutBindings);
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
    { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewer, "shaders/text_draw.vert.spv"), "main" },
    { VK_SHADER_STAGE_GEOMETRY_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/text_draw.geom.spv"), "main" },
    { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/text_draw.frag.spv"), "main" }
  };
  statisticsRoot->addChild(textPipeline);

  auto fontSampler = std::make_shared<pumex::Sampler>(pumex::SamplerTraits());

  // preparing two fonts and text nodes
  fontDefault = std::make_shared<pumex::Font>(viewer, "fonts/DejaVuSans.ttf", glm::uvec2(1024, 1024), 24, texturesAllocator);
  textDefault = std::make_shared<pumex::Text>(fontDefault, buffersAllocator);
  textDefault->setName("textDefault");
  textPipeline->addChild(textDefault);

  auto fontDefaultImageView = std::make_shared<pumex::ImageView>(fontDefault->fontMemoryImage, fontDefault->fontMemoryImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D);
  auto textDefaultDescriptorSet = std::make_shared<pumex::DescriptorSet>(textDescriptorSetLayout);
//  textDefaultDescriptorSet->setDescriptor(0, textCameraUbo);
  textDefaultDescriptorSet->setDescriptor(1, std::make_shared<pumex::CombinedImageSampler>(fontDefaultImageView, fontSampler));
  textDefault->setDescriptorSet(0, textDefaultDescriptorSet);

//  fontSmall = std::make_shared<pumex::Font>(viewer, "fonts/DejaVuSans.ttf", glm::uvec2(1024, 1024), 12, texturesAllocator);
//  textSmall = std::make_shared<pumex::Text>(fontSmall, buffersAllocator);
//  textSmall->setName("textSmall");
//  textPipeline->addChild(textSmall);
//
//  auto fontSmallImageView = std::make_shared<pumex::ImageView>(fontSmall->fontMemoryImage, fontSmall->fontMemoryImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D);
//  auto textSmallDescriptorSet = std::make_shared<pumex::DescriptorSet>(textDescriptorSetLayout);
////  textSmallDescriptorSet->setDescriptor(0, textCameraUbo);
//  textSmallDescriptorSet->setDescriptor(1, std::make_shared<pumex::CombinedImageSampler>(fontSmallImageView, fontSampler));
//  textSmall->setDescriptorSet(0, textSmallDescriptorSet);
}

void TimeStatisticsHandler::setTextCameraBuffer(std::shared_ptr<MemoryBuffer> memoryBuffer)
{
  auto textCameraUbo = std::make_shared<pumex::UniformBuffer>(memoryBuffer);
  textDefault->getDescriptorSet(0)->setDescriptor(0, textCameraUbo);
}


void TimeStatisticsHandler::collectData(Surface* surface, TimeStatistics* viewerStatistics, TimeStatistics* surfaceStatistics)
{
  const uint32_t TSH_FRAMES_RENDERED = 4;
  const uint32_t TSH_FPS_ID = 1;
  const auto& fpsChannel = viewerStatistics->getChannel(TSV_CHANNEL_FRAME);

  double fpsValue = 1.0 / fpsChannel.getAverageValue();
  std::wstringstream stream;
  stream << "FPS : " << std::fixed << std::setprecision(1) << fpsValue;
  textDefault->setText(surface, TSH_FPS_ID, glm::vec2(40, 40), glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), stream.str());
//  std::vector<double> fpsBegin, fpsDuration;
//  fpsChannel.getLastValues(TSH_FRAMES_RENDERED, fpsBegin, fpsDuration);


//  auto frameNumber = viewer.lock()->getFrameNumber();
//  if ((frameNumber % 256) == 128)
//  {
//    for (const auto& group : viewerTimeStatistics->getGroups())
//    {
//      LOG_ERROR << "Group : " << group.second << "\n";
//      auto channelIDs = viewerTimeStatistics->getGroupChannelIDs(group.first);
//      for (auto channelID : channelIDs)
//      {
//        const auto& channel = viewerTimeStatistics->getChannel(channelID);
//        LOG_ERROR << "  " << channel.getChannelName() << " : " << 1000.0 * channel.getMinValue() << "   " << 1000.0 * channel.getAverageValue() << "   " << 1000.0 * channel.getMaxValue() << "\n";
//      }
//    }
//    viewerTimeStatistics->resetMinMaxValues();
//
//    for (const auto& group : surfaceStatistics->getGroups())
//    {
//      LOG_ERROR << "Group : " << group.second << "\n";
//      auto channelIDs = surfaceStatistics->getGroupChannelIDs(group.first);
//      for (auto channelID : channelIDs)
//      {
//        const auto& channel = surfaceStatistics->getChannel(channelID);
//        LOG_ERROR << "  " << channel.getChannelName() << " : " << 1000.0 * channel.getMinValue() << "   " << 1000.0 * channel.getAverageValue() << "   " << 1000.0 * channel.getMaxValue() << "\n";
//      }
//    }
//    LOG_ERROR << std::endl;
//    surfaceStatistics->resetMinMaxValues();
//  }
}
