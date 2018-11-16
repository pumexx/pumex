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

#include <iomanip>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <args.hxx>
#include <pumex/Pumex.h>
#include <pumex/AssetLoaderAssimp.h>
#include <pumex/utils/Shapes.h>
#include "MainWindow.h"
#include <pumex/platform/qt/WindowQT.h>
#include <QtWidgets/QApplication>
#include <QtCore/QLoggingCategory>

// pumexviewer is a very basic program, that performs textureless rendering of a 3D asset provided in a command line
// The whole render workflow consists of only one render operation

const uint32_t MAX_BONES = 511;

struct PositionData
{
  PositionData()
    : color{ 1.0f, 1.0f, 1.0f, 1.0f }
  {
  }
  PositionData(const glm::mat4& p)
    : color{ 1.0f, 1.0f, 1.0f, 1.0f }, position { p }
  {
  }
  glm::vec4 color;
  glm::mat4 position;
  glm::mat4 bones[MAX_BONES];
};

class ViewerApplicationData
{
public:
  ViewerApplicationData( std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator, std::shared_ptr<pumex::DeviceMemoryAllocator> verticesAllocator, const std::vector<pumex::VertexSemantic>& requiredSemantic)
    : semantic(requiredSemantic)
  {
    // create buffers visible from renderer
    cameraBuffer     = std::make_shared<pumex::Buffer<pumex::Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce, true);
    textCameraBuffer = std::make_shared<pumex::Buffer<pumex::Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce, true);
    assetNode        = std::make_shared<pumex::AssetNode>(verticesAllocator, 1, 0);
    assetNode->setName("assetNode");
    boxAssetNode     = std::make_shared<pumex::AssetNode>(verticesAllocator, 1, 0);
    boxAssetNode->setName("boxAssetNode");
    positionData     = std::make_shared<PositionData>();
    positionBuffer   = std::make_shared<pumex::Buffer<PositionData>>(positionData, buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerDevice, pumex::swOnce);

    // create default model shown at startup
    pumex::Geometry defaultGeometry;
    defaultGeometry.name     = "defaultGeometry";
    defaultGeometry.semantic = semantic;
    pumex::addSphere(defaultGeometry, glm::vec3(0.0f, 0.0f, 0.0f), 1.0f, 16, 16, true);
    asset = pumex::createSimpleAsset(defaultGeometry, "defaultGeometry");
    assetNode->setAsset(asset);

    updateBoxAssetNode();
  }

  void setCameraHandler(std::shared_ptr<pumex::BasicCameraHandler> bcamHandler)
  {
    camHandler = bcamHandler;
  }

  void update(std::shared_ptr<pumex::Viewer> viewer)
  {
    camHandler->update(viewer.get());
  }

  void prepareCameraForRendering(std::shared_ptr<pumex::Surface> surface)
  {
    // prepare camera state for rendering
    auto viewer           = surface->viewer.lock();
    float deltaTime       = pumex::inSeconds(viewer->getRenderTimeDelta());
    float renderTime      = pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()) + deltaTime;
    uint32_t renderWidth  = surface->swapChainSize.width;
    uint32_t renderHeight = surface->swapChainSize.height;
    glm::mat4 viewMatrix  = camHandler->getViewMatrix(surface.get());

    pumex::Camera camera;
    camera.setViewMatrix(viewMatrix);
    camera.setObserverPosition(camHandler->getObserverPosition(surface.get()));
    camera.setTimeSinceStart(renderTime);
    camera.setProjectionMatrix(glm::perspective(glm::radians(60.0f), (float)renderWidth / (float)renderHeight, 0.1f, 100000.0f));
    cameraBuffer->setData(surface.get(), camera);

    pumex::Camera textCamera;
    textCamera.setProjectionMatrix(glm::ortho(0.0f, (float)renderWidth, 0.0f, (float)renderHeight), false);
    textCameraBuffer->setData(surface.get(), textCamera);
  }

  void prepareModelForRendering(pumex::Viewer* viewer)
  {
    actions.performActions();

    // animate asset if it has animation
    if (asset->animations.empty())
      return;

    float deltaTime          = pumex::inSeconds(viewer->getRenderTimeDelta());
    float renderTime         = pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()) + deltaTime;
    pumex::Animation& anim   = asset->animations[0];
    pumex::Skeleton& skel    = asset->skeleton;
    uint32_t numAnimChannels = anim.channels.size();
    uint32_t numSkelBones    = skel.bones.size();

    std::vector<uint32_t> boneChannelMapping(numSkelBones);
    for (uint32_t boneIndex = 0; boneIndex < numSkelBones; ++boneIndex)
    {
      auto it = anim.invChannelNames.find(skel.boneNames[boneIndex]);
      boneChannelMapping[boneIndex] = (it != end(anim.invChannelNames)) ? it->second : std::numeric_limits<uint32_t>::max();
    }

    std::vector<glm::mat4> localTransforms(MAX_BONES);
    std::vector<glm::mat4> globalTransforms(MAX_BONES);

    anim.calculateLocalTransforms(renderTime, localTransforms.data(), numAnimChannels);
    uint32_t bcVal = boneChannelMapping[0];
    glm::mat4 localCurrentTransform = (bcVal == std::numeric_limits<uint32_t>::max()) ? skel.bones[0].localTransformation : localTransforms[bcVal];
    globalTransforms[0] = skel.invGlobalTransform * localCurrentTransform;
    for (uint32_t boneIndex = 1; boneIndex < numSkelBones; ++boneIndex)
    {
      bcVal = boneChannelMapping[boneIndex];
      localCurrentTransform = (bcVal == std::numeric_limits<uint32_t>::max()) ? skel.bones[boneIndex].localTransformation : localTransforms[bcVal];
      globalTransforms[boneIndex] = globalTransforms[skel.bones[boneIndex].parentIndex] * localCurrentTransform;
    }
    for (uint32_t boneIndex = 0; boneIndex < numSkelBones; ++boneIndex)
      positionData->bones[boneIndex] = globalTransforms[boneIndex] * skel.bones[boneIndex].offsetMatrix;

    positionBuffer->invalidateData();
  }

  void setModelColor(const glm::vec4& color)
  {
    positionData->color = color;
    positionBuffer->invalidateData();
  }

  void loadModel(std::shared_ptr<pumex::Viewer> viewer, const std::string& modelFileName)
  {
    std::shared_ptr<pumex::Asset> loadedAsset;
    try
    {
      loadedAsset = loader.load(viewer, modelFileName, false, semantic);
    }
    catch (std::exception& e)
    {
      LOG_ERROR << "EXCEPTION during model loading : "<< e.what() << std::endl;
      return;
    }
    if (loadedAsset.get() == nullptr)
      return;
    // This method is performed in a GUI thread.
    // After loading a model we will send it to render thread ( setModel will be called during actions.performActions() )
    actions.addAction(std::bind(&ViewerApplicationData::setModel, this, loadedAsset));
  }

  void loadAnimation(std::shared_ptr<pumex::Viewer> viewer, const std::string& modelFileName)
  {
    std::shared_ptr<pumex::Asset> loadedAsset;
    try
    {
      loadedAsset = loader.load(viewer, modelFileName, false, semantic);
    }
    catch (std::exception& e)
    {
      LOG_ERROR << "EXCEPTION during model loading : " << e.what() << std::endl;
      return;
    }
    if (loadedAsset.get() == nullptr)
      return;
    if (loadedAsset->animations.empty())
    {
      LOG_ERROR << "No animations have been found in a file  : " << modelFileName << std::endl;
      return;
    }
    // This method is performed in a GUI thread.
    // After loading a model we will send it to render thread ( setAnimation will be called during actions.performActions() )
    if (loadedAsset.get() != nullptr)
      actions.addAction(std::bind(&ViewerApplicationData::setAnimation, this, loadedAsset));
  }

  void setModel(std::shared_ptr<pumex::Asset> loadedAsset)
  {
    auto animations   = asset->animations;
    asset             = loadedAsset;
    asset->animations = animations;
    assetNode->setAsset(asset);
    updateBoxAssetNode();
  }

  void setAnimation(std::shared_ptr<pumex::Asset> loadedAsset)
  {
    asset->animations = loadedAsset->animations;
    updateBoxAssetNode();
  }

  void updateBoxAssetNode()
  {
    pumex::BoundingBox bbox;
    if (asset->animations.size() > 0)
      bbox = pumex::calculateBoundingBox(asset->skeleton, asset->animations[0], true);
    else
      bbox = pumex::calculateBoundingBox(*asset, 1);

    // create a bounding box as a geometry to render
    pumex::Geometry boxg;
    boxg.name = "box";
    boxg.semantic = semantic;
    pumex::addBox(boxg, bbox.bbMin, bbox.bbMax, true);
    std::shared_ptr<pumex::Asset> boxAsset = pumex::createSimpleAsset(boxg, "root");
    boxAssetNode->setAsset(boxAsset);

    // is this the fastest way to calculate all global transformations for a model ?
    std::vector<glm::mat4> globalTransforms = pumex::calculateResetPosition(*asset);
    std::copy(begin(globalTransforms), end(globalTransforms), std::begin(positionData->bones));
  }

  std::vector<pumex::VertexSemantic>            semantic;
  std::shared_ptr<pumex::Buffer<pumex::Camera>> cameraBuffer;
  std::shared_ptr<pumex::Buffer<pumex::Camera>> textCameraBuffer;
  std::shared_ptr<pumex::Asset>                 asset;
  std::shared_ptr<pumex::AssetNode>             assetNode;
  std::shared_ptr<pumex::AssetNode>             boxAssetNode;
  std::shared_ptr<PositionData>                 positionData;
  std::shared_ptr<pumex::Buffer<PositionData>>  positionBuffer;
  std::shared_ptr<pumex::BasicCameraHandler>    camHandler;
  pumex::ActionQueue                            actions;
  pumex::AssetLoaderAssimp                      loader;
};

int main( int argc, char * argv[] )
{
  SET_LOG_INFO;
  QApplication application(argc, argv);

  // process command line using args library
  args::ArgumentParser                         parser("pumex example : minimal 3D model viewer without textures");
  args::HelpFlag                               help(parser, "help", "display this help menu", { 'h', "help" });
  args::Flag                                   enableDebugging(parser, "debug", "enable Vulkan debugging", { 'd' });
  args::MapFlag<std::string, VkPresentModeKHR> presentationMode(parser, "presentation_mode", "presentation mode (immediate, mailbox, fifo, fifo_relaxed)", { 'p' }, pumex::Surface::nameToPresentationModes, VK_PRESENT_MODE_FIFO_KHR);
  args::ValueFlag<uint32_t>                    updatesPerSecond(parser, "update_frequency", "number of update calls per second", { 'u' }, 60);
  try
  {
    parser.ParseCLI(argc, argv);
  }
  catch (const args::Help&)
  {
    LOG_ERROR << parser;
    FLUSH_LOG;
    return 0;
  }
  catch (const args::ParseError& e)
  {
    LOG_ERROR << e.what() << std::endl;
    LOG_ERROR << parser;
    FLUSH_LOG;
    return 1;
  }
  catch (const args::ValidationError& e)
  {
    LOG_ERROR << e.what() << std::endl;
    LOG_ERROR << parser;
    FLUSH_LOG;
    return 1;
  }
  VkPresentModeKHR presentMode  = args::get(presentationMode);
  uint32_t updateFrequency      = std::max(1U, args::get(updatesPerSecond));

  std::shared_ptr<pumex::Viewer> viewer;
  try
  {
    // We need to prepare ViewerTraits object. It stores all basic configuration for Vulkan instance ( Viewer class )
    std::vector<std::string> instanceExtensions;
    std::vector<std::string> requestDebugLayers;
    if (enableDebugging)
      requestDebugLayers.push_back("VK_LAYER_LUNARG_standard_validation");
    pumex::ViewerTraits viewerTraits{ "pumex viewer", instanceExtensions, requestDebugLayers, updateFrequency };
    viewerTraits.debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT;// | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;

    // Viewer object is created
    viewer = std::make_shared<pumex::Viewer>(viewerTraits);

    // now is the time to create devices, windows and surfaces.
    std::vector<std::string> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device = viewer->addDevice(0, requestDeviceExtensions);

    // alocate 16 MB for frame buffers
    std::shared_ptr<pumex::DeviceMemoryAllocator> frameBufferAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 16 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // alocate 1 MB for uniform and storage buffers
    std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 64 MB for vertex and index buffers
    std::shared_ptr<pumex::DeviceMemoryAllocator> verticesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 64 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 8 MB memory for font textures
    std::shared_ptr<pumex::DeviceMemoryAllocator> texturesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 8 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // create common descriptor pool
    std::shared_ptr<pumex::DescriptorPool> descriptorPool = std::make_shared<pumex::DescriptorPool>();

    // let's create QT window and assign it to main QT window ( see MainWindow.cpp for details )
    pumex::QWindowPumex*        pumexWindow = new pumex::QWindowPumex;
    std::shared_ptr<MainWindow> mainWindow = std::make_shared<MainWindow>(pumexWindow);
    // then we create a surface in this window
    pumex::SurfaceTraits surfaceTraits{ 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, presentMode, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    std::shared_ptr<pumex::Surface> surface = pumexWindow->getWindowQT()->createSurface(device, surfaceTraits);

    // render workflow will use one queue with below defined traits
    std::vector<pumex::QueueTraits> queueTraits{ { VK_QUEUE_GRAPHICS_BIT, 0, 0.75f } };

    pumex::ImageSize fullScreenSize{ pumex::ImageSize::SurfaceDependent, glm::vec2(1.0f,1.0f) };

    std::shared_ptr<pumex::RenderWorkflow> workflow = std::make_shared<pumex::RenderWorkflow>("viewer_workflow", frameBufferAllocator, queueTraits);
      workflow->addResourceType("depth_samples", VK_FORMAT_D32_SFLOAT,     fullScreenSize, pumex::atDepth,   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, false);
      workflow->addResourceType("surface",       VK_FORMAT_B8G8R8A8_UNORM, fullScreenSize, pumex::atSurface, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,         true);

    // workflow will only have one operation that has two output attachments : depth buffer and swapchain image
    workflow->addRenderOperation("rendering", pumex::RenderOperation::Graphics);
      workflow->addAttachmentDepthOutput( "rendering", "depth_samples", "depth", pumex::ImageSubresourceRange(), pumex::loadOpClear(glm::vec2(1.0f, 0.0f)));
      workflow->addAttachmentOutput(      "rendering", "surface",       "color", pumex::ImageSubresourceRange(), pumex::loadOpClear(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)));

    // vertex semantic defines how a single vertex in an asset will look like
    std::vector<pumex::VertexSemantic> requiredSemantic = { { pumex::VertexSemantic::Position, 3 },{ pumex::VertexSemantic::Normal, 3 },{ pumex::VertexSemantic::TexCoord, 2 },{ pumex::VertexSemantic::BoneWeight, 4 },{ pumex::VertexSemantic::BoneIndex, 4 } };

    // Application data class stores all information required to update rendering ( animation state, camera position, etc )
    std::shared_ptr<ViewerApplicationData> applicationData = std::make_shared<ViewerApplicationData>(buffersAllocator, verticesAllocator, requiredSemantic);

    // Connect MainWindow signals to ViewerApplicationData methods. 
    // Caution : lambda is not a proper QT receiver and connection will not be automatically closed when applicationData goes out of scope.
    // I used this construction, because MainWindow and ViewerApplicationData go aout of scope in the same time, so there is little chance for signal to be emitted.
    // Moreover - I used QT signals because I didn't want to add ViewerApplicationData dependency to MainWindow.
    QObject::connect(mainWindow.get(), &MainWindow::signalSetModelColor, [&](const glm::vec4& color)      { applicationData->setModelColor(color); });
    QObject::connect(mainWindow.get(), &MainWindow::signalLoadModel,     [&](const std::string& fileName) { applicationData->loadModel(viewer, fileName); });
    QObject::connect(mainWindow.get(), &MainWindow::signalLoadAnimation, [&](const std::string& fileName) { applicationData->loadAnimation(viewer, fileName); });

    // our render operation named "rendering" must have scenegraph attached
    auto renderRoot = std::make_shared<pumex::Group>();
    renderRoot->setName("renderRoot");
    workflow->setRenderOperationNode("rendering", renderRoot);

    // If render operation is defined as graphics operation ( pumex::RenderOperation::Graphics ) then scene graph must have :
    // - at least one graphics pipeline
    // - at least one vertex buffer ( and if we use nodes calling vkCmdDrawIndexed* then index buffer is also required )
    // - at least one node that calls one of vkCmdDraw* commands
    //
    // In case of compute operations the scene graph must have :
    // - at least one compute pipeline
    // - at least one node calling vkCmdDispatch
    //
    // Here is the simple definition of graphics pipeline infrastructure : descriptor set layout, pipeline layout, pipeline cache, shaders and graphics pipeline itself :
    // Shaders will use two uniform buffers ( both in vertex shader )
    std::vector<pumex::DescriptorSetLayoutBinding> layoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
    };
    auto descriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(layoutBindings);

    // building pipeline layout
    auto pipelineLayout = std::make_shared<pumex::PipelineLayout>();
    pipelineLayout->descriptorSetLayouts.push_back(descriptorSetLayout);

    auto pipelineCache = std::make_shared<pumex::PipelineCache>();

    auto pipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, pipelineLayout);
    // loading vertex and fragment shader
    pipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/viewerqt_basic.vert.spv"), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/viewerqt_basic.frag.spv"), "main" }
    };
    // vertex input - we will use the same vertex semantic that the loaded model has
    pipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
    };
    pipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    renderRoot->addChild(pipeline);

    pipeline->addChild(applicationData->assetNode);

    // Our additional pipeline will draw a wireframe bounding box using polygon mode VK_POLYGON_MODE_LINE using the same shaders
    auto wireframePipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, pipelineLayout);
    wireframePipeline->polygonMode = VK_POLYGON_MODE_LINE;
    wireframePipeline->cullMode = VK_CULL_MODE_NONE;
    wireframePipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/viewerqt_basic.vert.spv"), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/viewerqt_basic.frag.spv"), "main" }
    };
    wireframePipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
    };
    wireframePipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    renderRoot->addChild(wireframePipeline);

    wireframePipeline->addChild(applicationData->boxAssetNode);

    // here we create above mentioned uniform buffers - one for camera state and one for model state
    auto cameraUbo   = std::make_shared<pumex::UniformBuffer>(applicationData->cameraBuffer);
    auto positionUbo = std::make_shared<pumex::UniformBuffer>(applicationData->positionBuffer);

    auto descriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorPool, descriptorSetLayout);
      descriptorSet->setDescriptor(0, cameraUbo);
      descriptorSet->setDescriptor(1, positionUbo);
    pipeline->setDescriptorSet(0, descriptorSet);

    auto wireframeDescriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorPool, descriptorSetLayout);
      wireframeDescriptorSet->setDescriptor(0, cameraUbo);
      wireframeDescriptorSet->setDescriptor(1, positionUbo);
    wireframePipeline->setDescriptorSet(0, wireframeDescriptorSet);

    // lets add object that calculates time statistics and is able to render it
    std::shared_ptr<pumex::TimeStatisticsHandler> tsHandler = std::make_shared<pumex::TimeStatisticsHandler>(viewer, pipelineCache, buffersAllocator, texturesAllocator, applicationData->textCameraBuffer);
    viewer->addInputEventHandler(tsHandler);
    renderRoot->addChild(tsHandler->getRoot());

    // camera handler processes input events at the beggining of the update phase
    std::shared_ptr<pumex::BasicCameraHandler> bcamHandler = std::make_shared<pumex::BasicCameraHandler>();
    viewer->addInputEventHandler(bcamHandler);
    applicationData->setCameraHandler(bcamHandler);

    std::shared_ptr<pumex::SingleQueueWorkflowCompiler> workflowCompiler = std::make_shared<pumex::SingleQueueWorkflowCompiler>();
    // We must connect update graph that works independently from render graph
    tbb::flow::continue_node< tbb::flow::continue_msg > update(viewer->updateGraph, [=](tbb::flow::continue_msg)
    {
      applicationData->update(viewer);
    });
    tbb::flow::make_edge(viewer->opStartUpdateGraph, update);
    tbb::flow::make_edge(update, viewer->opEndUpdateGraph);

    if (enableDebugging)
      QLoggingCategory::setFilterRules(QStringLiteral("qt.vulkan=true"));

    // each surface may have its own workflow and a compiler that transforms workflow into Vulkan usable entity
    surface->setRenderWorkflow(workflow, workflowCompiler);
    // events are used to call application data update methods. These methods generate data visisble by renderer through uniform buffers
    viewer->setEventRenderStart(std::bind(&ViewerApplicationData::prepareModelForRendering, applicationData, std::placeholders::_1));
    surface->setEventSurfaceRenderStart(std::bind(&ViewerApplicationData::prepareCameraForRendering, applicationData, std::placeholders::_1));
    // object calculating statistics must be also connected as an event
    surface->setEventSurfacePrepareStatistics(std::bind(&pumex::TimeStatisticsHandler::collectData, tsHandler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    mainWindow->show();
    std::thread viewerThread([&]
    {
        viewer->run();
    }
    );
    application.exec();
    viewer->setTerminate();
    viewerThread.join();
  }
  catch (const std::exception& e)
  {
#if defined(_DEBUG) && defined(_WIN32)
    OutputDebugStringA("Exception thrown : ");
    OutputDebugStringA(e.what());
    OutputDebugStringA("\n");
#endif
    LOG_ERROR << "Exception thrown : " << e.what() << std::endl;
  }
  catch (...)
  {
#if defined(_DEBUG) && defined(_WIN32)
    OutputDebugStringA("Unknown error\n");
#endif
    LOG_ERROR << "Unknown error" << std::endl;
  }
  // here are all windows, surfaces, devices, workflows and scene graphs destroyed
  viewer->cleanup();
  FLUSH_LOG;
  return 0;
}
