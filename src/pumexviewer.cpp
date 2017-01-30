#include <pumex/Pumex.h>
#include <pumex/AssetLoaderAssimp.h>
#include <pumex/utils/Shapes.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

const uint32_t MAX_BONES = 63;

struct PositionData
{
  PositionData()
  {
  }
  PositionData(const glm::mat4& p)
    : position{ p }
  {
  }
  glm::mat4 position;
  glm::mat4 bones[MAX_BONES];
};

struct ApplicationData
{
  ApplicationData(const std::string& mName, std::shared_ptr<pumex::Viewer> v)
    : viewer{v}
  {
    modelName = viewer->getFullFilePath(mName);

  }
  void setup()
  {
    std::vector<pumex::VertexSemantic> requiredSemantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 2 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };
    std::vector<pumex::VertexSemantic> boxSemantic = requiredSemantic;//{ { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 2 } };
    assetBuffer.registerVertexSemantic(1, requiredSemantic);
    boxAssetBuffer.registerVertexSemantic(1, boxSemantic);
    pumex::AssetLoaderAssimp loader;
    std::shared_ptr<pumex::Asset> asset(loader.load(modelName, false, requiredSemantic));
    CHECK_LOG_THROW (asset.get() == nullptr,  "Model not loaded : " << modelName);

    pumex::BoundingBox bbox;
    if (asset->animations.size() > 0)
      bbox = pumex::calculateBoundingBox(asset->skeleton, asset->animations[0], true);
    else
      bbox = pumex::calculateBoundingBox(*asset,1);

    pumex::Geometry boxg;
    boxg.name     = "box";
    boxg.semantic = boxSemantic;
    pumex::addBox(boxg, bbox.bbMin, bbox.bbMax);
    std::shared_ptr<pumex::Asset> boxAsset(pumex::createSimpleAsset(boxg, "root"));

    pumex::Geometry cone;
    cone.name     = "cone";
    cone.semantic = requiredSemantic;
    pumex::addCone(cone, glm::vec3(0, 0, 0), 0.1f, 0.1f, 16, 8, true);
    std::shared_ptr<pumex::Asset> testAsset(pumex::createSimpleAsset(cone, "root"));

    pumex::BoundingBox testFigureBbox = pumex::calculateBoundingBox(*testAsset,1);

    modelTypeID = assetBuffer.registerType("object", pumex::AssetTypeDefinition(bbox));
    assetBuffer.registerObjectLOD(modelTypeID, asset, pumex::AssetLodDefinition(0.0f, 10000.0f));

    boxTypeID = boxAssetBuffer.registerType("objectBox", pumex::AssetTypeDefinition(bbox));
    boxAssetBuffer.registerObjectLOD(boxTypeID, boxAsset, pumex::AssetLodDefinition(0.0f, 10000.0f));

    testFigureTypeID = assetBuffer.registerType("testFigure", pumex::AssetTypeDefinition(testFigureBbox));
    assetBuffer.registerObjectLOD(testFigureTypeID, testAsset, pumex::AssetLodDefinition(0.0f, 10000.0f));

    std::vector<pumex::DescriptorSetLayoutBinding> layoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
    };
    descriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(layoutBindings);

    descriptorPool = std::make_shared<pumex::DescriptorPool>(10, layoutBindings);

    // building pipeline layout
    pipelineLayout = std::make_shared<pumex::PipelineLayout>();
    pipelineLayout->descriptorSetLayouts.push_back(descriptorSetLayout);

    pipelineCache = std::make_shared<pumex::PipelineCache>();

    pipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, pipelineLayout, defaultRenderPass, 0);
    pipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("viewer_basic.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("viewer_basic.frag.spv")), "main" }
    };
    pipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
    };
    pipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    pipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    boxPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, pipelineLayout, defaultRenderPass, 0);
    boxPipeline->polygonMode  = VK_POLYGON_MODE_LINE;
    boxPipeline->cullMode     = VK_CULL_MODE_NONE;
    boxPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("viewer_basic.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("viewer_basic.frag.spv")), "main" }
    };
    boxPipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, boxSemantic }
    };
    boxPipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    boxPipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    cameraUbo = std::make_shared<pumex::UniformBuffer<pumex::Camera>>();

    // is this the fastest way to calculate all global transformations for a model ?
    std::vector<glm::mat4> globalTransforms = pumex::calculateResetPosition(*asset);
    PositionData modelData;
    std::copy(globalTransforms.begin(), globalTransforms.end(), std::begin(modelData.bones));

    positionUbo = std::make_shared<pumex::UniformBuffer<PositionData>>(modelData);
    descriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorSetLayout, descriptorPool);
    descriptorSet->setSource(0, cameraUbo);
    descriptorSet->setSource(1, positionUbo);
    boxDescriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorSetLayout, descriptorPool);
    boxDescriptorSet->setSource(0, cameraUbo);
    boxDescriptorSet->setSource(1, positionUbo);
  }
  void update(float timeSinceStart)
  {
    std::shared_ptr<pumex::Asset> assetX = assetBuffer.getAsset(modelTypeID, 0);
    if (assetX->animations.size() <1)
      return;

    PositionData modelData = positionUbo->get();
    positionUbo->set(modelData);
   
  }
  std::shared_ptr<pumex::Viewer> viewer;
  std::string modelName;
  uint32_t    modelTypeID;
  uint32_t    boxTypeID;
  uint32_t    testFigureTypeID;
  std::shared_ptr<pumex::UniformBuffer<pumex::Camera>> cameraUbo;
  std::shared_ptr<pumex::UniformBuffer<PositionData>> positionUbo;

  pumex::AssetBuffer                          assetBuffer;
  pumex::AssetBuffer                          boxAssetBuffer;
  std::shared_ptr<pumex::RenderPass>          defaultRenderPass;
  std::shared_ptr<pumex::DescriptorSetLayout> descriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>      pipelineLayout;
  std::shared_ptr<pumex::PipelineCache>       pipelineCache;
  std::shared_ptr<pumex::GraphicsPipeline>    pipeline;
  std::shared_ptr<pumex::GraphicsPipeline>    boxPipeline;
  std::shared_ptr<pumex::DescriptorPool>      descriptorPool;
  std::shared_ptr<pumex::DescriptorSet>       descriptorSet;
  std::shared_ptr<pumex::DescriptorSet>       boxDescriptorSet;
};

class ViewerThread : public pumex::SurfaceThread
{
public:
  ViewerThread( std::shared_ptr<ApplicationData> applicationData )
    : pumex::SurfaceThread(), appData(applicationData)
  {
  }

  void setup(std::shared_ptr<pumex::Surface> s) override
  {
    SurfaceThread::setup(s);

    std::shared_ptr<pumex::Surface> surfaceSh = surface.lock();
    std::shared_ptr<pumex::Device>  deviceSh = surfaceSh->device.lock();
    VkDevice                        vkDevice = deviceSh->device;

    myCmdBuffer = std::make_shared<pumex::CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, surfaceSh->commandPool);
    myCmdBuffer->validate(deviceSh);

    appData->cameraUbo->validate(deviceSh);
    appData->positionUbo->validate(deviceSh);

    // loading models
    appData->assetBuffer.validate(deviceSh, true, surfaceSh->commandPool, surfaceSh->presentationQueue);
    appData->boxAssetBuffer.validate(deviceSh, true, surfaceSh->commandPool, surfaceSh->presentationQueue);
    appData->descriptorSetLayout->validate(deviceSh);
    appData->descriptorPool->validate(deviceSh);
    appData->pipelineLayout->validate(deviceSh);
    appData->pipelineCache->validate(deviceSh);
    appData->pipeline->validate(deviceSh);
    appData->boxPipeline->validate(deviceSh);

    // preparing descriptor sets
    appData->descriptorSet->validate(deviceSh);
    appData->boxDescriptorSet->validate(deviceSh);

    cameraPosition              = glm::vec3(0.0f, 0.0f, 0.0f);
    cameraGeographicCoordinates = glm::vec2(0.0f, 0.0f);
    cameraDistance              = 1.0f;
    leftMouseKeyPressed         = false;
    rightMouseKeyPressed        = false;
  }

  void cleanup() override
  {
    SurfaceThread::cleanup();
  }
  ~ViewerThread()
  {
    cleanup();
  }
  void draw() override
  {
    std::shared_ptr<pumex::Surface> surfaceSh = surface.lock();
    std::shared_ptr<pumex::Viewer>  viewerSh  = surfaceSh->viewer.lock();
    std::shared_ptr<pumex::Device>  deviceSh  = surfaceSh->device.lock();
    std::shared_ptr<pumex::Window>  windowSh  = surfaceSh->window.lock();
    VkDevice                        vkDevice  = deviceSh->device;

    double timeSinceStartInSeconds = std::chrono::duration<double, std::ratio<1,1>>(timeSinceStart).count();
    double lastFrameInSeconds      = std::chrono::duration<double, std::ratio<1,1>>(timeSinceLastFrame).count();

    appData->update(timeSinceStartInSeconds);

    // camera update
    std::vector<pumex::MouseEvent> mouseEvents = windowSh->getMouseEvents();
    glm::vec2 mouseMove = lastMousePos;
    for (const auto& m : mouseEvents)
    {
      switch (m.type)
      {
      case pumex::MouseEvent::KEY_PRESSED:
        if (m.button == pumex::MouseEvent::LEFT)
          leftMouseKeyPressed = true;
        if (m.button == pumex::MouseEvent::RIGHT)
          rightMouseKeyPressed = true;
        mouseMove.x = m.x;
        mouseMove.y = m.y;
        lastMousePos = mouseMove;
        break;
      case pumex::MouseEvent::KEY_RELEASED:
        if (m.button == pumex::MouseEvent::LEFT)
          leftMouseKeyPressed = false;
        if (m.button == pumex::MouseEvent::RIGHT)
          rightMouseKeyPressed = false;
        break;
      case pumex::MouseEvent::MOVE:
        if (leftMouseKeyPressed || rightMouseKeyPressed)
        {
          mouseMove.x = m.x;
          mouseMove.y = m.y;
        }
        break;
      }
    }
    if (leftMouseKeyPressed)
    {
      cameraGeographicCoordinates.x -= 100.0f*(mouseMove.x - lastMousePos.x);
      cameraGeographicCoordinates.y += 100.0f*(mouseMove.y - lastMousePos.y);
      while (cameraGeographicCoordinates.x < -180.0f)
        cameraGeographicCoordinates.x += 360.0f;
      while (cameraGeographicCoordinates.x>180.0f)
        cameraGeographicCoordinates.x -= 360.0f;
      cameraGeographicCoordinates.y = glm::clamp(cameraGeographicCoordinates.y, -90.0f, 90.0f);
      lastMousePos = mouseMove;
    }
    if (rightMouseKeyPressed)
    {
      cameraDistance += 10.0f*(lastMousePos.y - mouseMove.y);
      if (cameraDistance<0.1f)
        cameraDistance = 0.1f;
      lastMousePos = mouseMove;
    }

    glm::vec3 forward = glm::vec3(cos(cameraGeographicCoordinates.x * 3.1415f / 180.0f), sin(cameraGeographicCoordinates.x * 3.1415f / 180.0f), 0) * 0.2f;
    glm::vec3 right = glm::vec3(cos((cameraGeographicCoordinates.x + 90.0f) * 3.1415f / 180.0f), sin((cameraGeographicCoordinates.x + 90.0f) * 3.1415f / 180.0f), 0) * 0.2f;
    if (windowSh->isKeyPressed('W'))
      cameraPosition -= forward;
    if (windowSh->isKeyPressed('S'))
      cameraPosition += forward;
    if (windowSh->isKeyPressed('A'))
      cameraPosition -= right;
    if (windowSh->isKeyPressed('D'))
      cameraPosition += right;

    glm::vec3 eye
      (
      cameraDistance * cos(cameraGeographicCoordinates.x * 3.1415f / 180.0f) * cos(cameraGeographicCoordinates.y * 3.1415f / 180.0f),
      cameraDistance * sin(cameraGeographicCoordinates.x * 3.1415f / 180.0f) * cos(cameraGeographicCoordinates.y * 3.1415f / 180.0f),
      cameraDistance * sin(cameraGeographicCoordinates.y * 3.1415f / 180.0f)
      );
    glm::mat4 viewMatrix = glm::lookAt(glm::vec3(eye) + cameraPosition, cameraPosition, glm::vec3(0, 0, 1));

    uint32_t renderWidth = surfaceSh->swapChainSize.width;
    uint32_t renderHeight = surfaceSh->swapChainSize.height;

    pumex::Camera camera = appData->cameraUbo->get();
    camera.setViewMatrix(viewMatrix);
    camera.setObserverPosition(eye);
    camera.setProjectionMatrix(glm::perspective(glm::radians(60.0f), (float)renderWidth / (float)renderHeight, 0.1f, 100000.0f));
    appData->cameraUbo->set(camera);

    appData->cameraUbo->validate(deviceSh);
    appData->positionUbo->validate(deviceSh);

    myCmdBuffer->cmdBegin(deviceSh);

    std::vector<VkClearValue> clearValues = { pumex::makeColorClearValue(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), pumex::makeDepthStencilClearValue(1.0f, 0) };
    myCmdBuffer->cmdBeginRenderPass(deviceSh, appData->defaultRenderPass, surfaceSh->getCurrentFrameBuffer(), pumex::makeVkRect2D(0, 0, renderWidth, renderHeight), clearValues);
    myCmdBuffer->cmdSetViewport(deviceSh, 0, { pumex::makeViewport(0, 0, renderWidth, renderHeight, 0.0f, 1.0f) });
    myCmdBuffer->cmdSetScissor(deviceSh, 0, { pumex::makeVkRect2D(0, 0, renderWidth, renderHeight) });

    myCmdBuffer->cmdBindPipeline(deviceSh, appData->pipeline);
    myCmdBuffer->cmdBindDescriptorSets(deviceSh, VK_PIPELINE_BIND_POINT_GRAPHICS, appData->pipelineLayout, 0, appData->descriptorSet);
    appData->assetBuffer.cmdBindVertexIndexBuffer(deviceSh, myCmdBuffer, 1, 0);
    appData->assetBuffer.cmdDrawObject(deviceSh, myCmdBuffer, 1, appData->modelTypeID, 0, 50.0f);
    appData->assetBuffer.cmdDrawObject(deviceSh, myCmdBuffer, 1, appData->testFigureTypeID, 0, 50.0f);

    myCmdBuffer->cmdBindPipeline(deviceSh, appData->boxPipeline);
    myCmdBuffer->cmdBindDescriptorSets(deviceSh, VK_PIPELINE_BIND_POINT_GRAPHICS, appData->pipelineLayout, 0, appData->boxDescriptorSet);
    appData->boxAssetBuffer.cmdBindVertexIndexBuffer(deviceSh, myCmdBuffer, 1, 0);
    appData->boxAssetBuffer.cmdDrawObject(deviceSh, myCmdBuffer, 1, appData->boxTypeID, 0, 50.0f);

    myCmdBuffer->cmdEndRenderPass(deviceSh);
    myCmdBuffer->cmdEnd(deviceSh);
    myCmdBuffer->queueSubmit(deviceSh, surfaceSh->presentationQueue, { surfaceSh->imageAvailableSemaphore }, { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }, { surfaceSh->renderCompleteSemaphore }, VK_NULL_HANDLE);
  }

  std::shared_ptr<ApplicationData>      appData;
  std::shared_ptr<pumex::CommandBuffer> myCmdBuffer;

  glm::vec3 cameraPosition;
  glm::vec2 cameraGeographicCoordinates;
  float     cameraDistance;
  glm::vec2 lastMousePos;
  bool      leftMouseKeyPressed;
  bool      rightMouseKeyPressed;
};

int main( int argc, char * argv[] )
{
  SET_LOG_ERROR;
  if (argc < 2)
  {
    LOG_WARNING << "Model filename not defined" << std::endl;
    return 1;
  }

  std::string windowName = "Pumex viewer : ";
  windowName += argv[1];
  std::string appName = "pumex viewer";
	
  pumex::ViewerTraits viewerTraits{ "pumex viewer", true, { { "VK_LAYER_LUNARG_standard_validation" } } };
  std::shared_ptr<pumex::Viewer> viewer = std::make_shared<pumex::Viewer>(viewerTraits);
  try
  {
    std::vector<pumex::QueueTraits> requestQueues = { pumex::QueueTraits{ VK_QUEUE_GRAPHICS_BIT , 0, { 0.75f } } };
    std::vector<const char*> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device = viewer->addDevice(0, requestQueues, requestDeviceExtensions);
    CHECK_LOG_THROW(!device->isValid(), "Cannot create logical device with requested parameters");

    pumex::WindowTraits windowTraits{ 0, 100, 100, 640, 480, false, windowName };
    std::shared_ptr<pumex::Window> window = pumex::Window::createWindow(windowTraits);

    pumex::SurfaceTraits surfaceTraits{ 3, VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, VK_FORMAT_D24_UNORM_S8_UINT, VK_PRESENT_MODE_FIFO_KHR, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    surfaceTraits.definePresentationQueue(pumex::QueueTraits{ VK_QUEUE_GRAPHICS_BIT, 0, { 0.75f } });

    std::vector<pumex::AttachmentDefinition> renderPassAttachments = 
    {
      { VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0 },
      { VK_FORMAT_D24_UNORM_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0 }
    };
    std::vector<pumex::SubpassDefinition> renderPassSubpasses = 
    {
      {
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        {},
        { { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
        {},
        { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL },
        {},
        0
      }
    };
    std::vector<pumex::SubpassDependencyDefinition> renderPassDependencies;
    std::shared_ptr<pumex::RenderPass> renderPass = std::make_shared<pumex::RenderPass>(renderPassAttachments, renderPassSubpasses, renderPassDependencies);
    surfaceTraits.setDefaultRenderPass(renderPass);

    std::shared_ptr<ApplicationData> applicationData = std::make_shared<ApplicationData>(argv[1], viewer);
    applicationData->defaultRenderPass = renderPass;
    applicationData->setup();

    std::shared_ptr<pumex::SurfaceThread> thread0 = std::make_shared<ViewerThread>(applicationData);
    std::shared_ptr<pumex::Surface> surface = viewer->addSurface(window, device, surfaceTraits, thread0);

    viewer->run();
  }
  catch (const std::exception e)
  {
  }
  catch (...)
  {
  }
  viewer->cleanup();
  FLUSH_LOG;
	return 0;
}