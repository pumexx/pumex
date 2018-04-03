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

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <pumex/Pumex.h>
#include <pumex/AssetLoaderAssimp.h>
#include <pumex/utils/Shapes.h>
#include <pumex/utils/Clipmap3.h>

// suppression of noexcept keyword used in args library ( so that code may compile on VS 2013 )
#ifdef _MSC_VER
  #if _MSC_VER<1900
    #define noexcept 
  #endif
#endif
#include <args.hxx>

// pumexvoxelizer is a test area for voxelization algorithms ( creation and rendering )

const uint32_t MAX_BONES = 511;

const uint32_t CLIPMAP_TEXTURE_COUNT = 4;
const uint32_t CLIPMAP_TEXTURE_SIZE  = 128;


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

struct UpdateData
{
  UpdateData()
  {
  }
  glm::vec3 cameraPosition;
  glm::vec2 cameraGeographicCoordinates;
  float     cameraDistance;

  glm::vec2 lastMousePos;
  bool      leftMouseKeyPressed;
  bool      rightMouseKeyPressed;
  bool      moveForward;
  bool      moveBackward;
  bool      moveLeft;
  bool      moveRight;
};

struct RenderData
{
  RenderData()
    : prevCameraDistance{ 1.0f }, cameraDistance{ 1.0f }
  {
  }
  glm::vec3               prevCameraPosition;
  glm::vec2               prevCameraGeographicCoordinates;
  float                   prevCameraDistance;
  glm::vec3               cameraPosition;
  glm::vec2               cameraGeographicCoordinates;
  float                   cameraDistance;
};



struct VoxelizerApplicationData
{
  VoxelizerApplicationData(std::shared_ptr<pumex::Viewer> v, const std::string& mName, const std::string& aName)
    : viewer{v}
  {
    modelName     = viewer->getFullFilePath(mName);
    if(!aName.empty())
      animationName = viewer->getFullFilePath(aName);
  }
  void setup()
  {
    // alocate 1 MB for uniform and storage buffers
    buffersAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT , 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 64 MB for vertex and index buffers
    verticesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 64 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);

    std::vector<pumex::VertexSemantic> requiredSemantic           = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 2 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };
    std::vector<pumex::VertexSemantic> boxSemantic                = requiredSemantic;//{ { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 2 } };
    std::vector<pumex::AssetBufferVertexSemantics> assetSemantics = { { 1, requiredSemantic } };
    std::vector<pumex::AssetBufferVertexSemantics> boxSemantics   = { { 1, boxSemantic } };

    assetBuffer    = std::make_shared<pumex::AssetBuffer>(assetSemantics, buffersAllocator, verticesAllocator);
    pumex::AssetLoaderAssimp loader;
    std::shared_ptr<pumex::Asset> asset(loader.load(modelName, false, requiredSemantic));
    CHECK_LOG_THROW (asset.get() == nullptr,  "Model not loaded : " << modelName);
    if (!animationName.empty())
    {
      std::shared_ptr<pumex::Asset> animAsset(loader.load(animationName, true, requiredSemantic));
      CHECK_LOG_THROW(animAsset.get() == nullptr, "Model with animation not loaded : " << animationName);
      asset->animations = animAsset->animations;
    }

    pumex::BoundingBox bbox;
    if (asset->animations.size() > 0)
      bbox = pumex::calculateBoundingBox(asset->skeleton, asset->animations[0], true);
    else
      bbox = pumex::calculateBoundingBox(*asset,1);

    modelTypeID = assetBuffer->registerType("object", pumex::AssetTypeDefinition(bbox));
    assetBuffer->registerObjectLOD(modelTypeID, asset, pumex::AssetLodDefinition(0.0f, 10000.0f));

    voxelBoundingBox = pumex::BoundingBox(glm::vec3(-10.0f, -10.0f, 0.0f), glm::vec3(10.0f, 10.0f, 20.0f));

    pumex::Geometry voxelBox;
    voxelBox.name = "voxelBox";
    voxelBox.semantic = requiredSemantic;
    voxelBox.materialIndex = 0;
    pumex::addBox(voxelBox, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), false);
    std::shared_ptr<pumex::Asset> voxelBoxAsset(pumex::createSimpleAsset(voxelBox, "voxelBox"));

    voxelBoxTypeID = assetBuffer->registerType("voxelBox", pumex::AssetTypeDefinition(pumex::BoundingBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f))));
    assetBuffer->registerObjectLOD(voxelBoxTypeID, voxelBoxAsset, pumex::AssetLodDefinition(0.0f, 10000.0f));

    // allocate memory for clipmaps
    clipmapAllocator     = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, CLIPMAP_TEXTURE_COUNT * CLIPMAP_TEXTURE_SIZE * CLIPMAP_TEXTURE_SIZE * CLIPMAP_TEXTURE_SIZE * 4 * 2, pumex::DeviceMemoryAllocator::FIRST_FIT);
    pumex::ImageTraits   clipMapIt(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_B8G8R8A8_UNORM, VkExtent3D{}, false, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, VK_IMAGE_TYPE_3D, VK_SHARING_MODE_EXCLUSIVE, VK_IMAGE_VIEW_TYPE_3D);
    pumex::SamplerTraits clipMapTt(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, VK_FALSE, 8, false, VK_COMPARE_OP_NEVER, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, false);
    clipMap = std::make_shared<pumex::Clipmap3>(CLIPMAP_TEXTURE_COUNT, CLIPMAP_TEXTURE_SIZE, pumex::makeColorClearValue(glm::vec4(0.0f,0.0f,0.0f,0.0f)), clipMapIt, clipMapTt, clipmapAllocator );

    // create render pass for voxelization
    std::vector<pumex::FrameBufferImageDefinition> voxelizeFrameBufferDefinitions =
    {
    };
    voxelizeFrameBufferImages = std::make_shared<pumex::FrameBufferImages>(voxelizeFrameBufferDefinitions, clipmapAllocator);
    std::vector<pumex::AttachmentDefinition> voxelizeRenderPassAttachments =
    {
    };
    std::vector<pumex::SubpassDefinition> voxelizeRenderPassSubpasses =
    {
      {
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        {},
        {},
        {},
        { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL },
        {},
        0
      }
    };
    std::vector<pumex::SubpassDependencyDefinition> voxelizeRenderPassDependencies;
    voxelizeRenderPass  = std::make_shared<pumex::RenderPass>(voxelizeRenderPassAttachments, voxelizeRenderPassSubpasses, voxelizeRenderPassDependencies);
    voxelizeFrameBuffer = std::make_unique<pumex::FrameBuffer>(voxelizeRenderPass, voxelizeFrameBufferImages);

    // create pipeline cache
    pipelineCache = std::make_shared<pumex::PipelineCache>();

    // create pipeline for voxelization
    std::vector<pumex::DescriptorSetLayoutBinding> voxelizeLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 2, CLIPMAP_TEXTURE_COUNT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    voxelizeDescriptorSetLayout   = std::make_shared<pumex::DescriptorSetLayout>(voxelizeLayoutBindings);
    voxelizePipelineLayout        = std::make_shared<pumex::PipelineLayout>();
    voxelizePipelineLayout->descriptorSetLayouts.push_back(voxelizeDescriptorSetLayout);
    voxelizeDescriptorPool        = std::make_shared<pumex::DescriptorPool>(2, voxelizeLayoutBindings);
    voxelizePipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, voxelizePipelineLayout, voxelizeRenderPass, 0);
    voxelizePipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
    };
    voxelizePipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/voxelizer_voxelize.vert.spv")), "main" },
      { VK_SHADER_STAGE_GEOMETRY_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/voxelizer_voxelize.geom.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/voxelizer_voxelize.frag.spv")), "main" }
    };
    voxelizePipeline->cullMode = VK_CULL_MODE_NONE;
    voxelizePipeline->depthTestEnable = VK_FALSE;
    voxelizePipeline->depthWriteEnable = VK_FALSE;
    voxelizePipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    // create pipeline for ray marching
    std::vector<pumex::DescriptorSetLayoutBinding> raymarchLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
      { 2, CLIPMAP_TEXTURE_COUNT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    raymarchDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(raymarchLayoutBindings);
    raymarchPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    raymarchPipelineLayout->descriptorSetLayouts.push_back(raymarchDescriptorSetLayout);
    raymarchDescriptorPool = std::make_shared<pumex::DescriptorPool>(2, raymarchLayoutBindings);
    raymarchPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, raymarchPipelineLayout, defaultRenderPass, 0);
    raymarchPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/voxelizer_raymarch.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/voxelizer_raymarch.frag.spv")), "main" }
    };
    raymarchPipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
    };
    raymarchPipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    raymarchPipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    // create pipeline for basic model rendering
    std::vector<pumex::DescriptorSetLayoutBinding> layoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
    };
    descriptorSetLayout    = std::make_shared<pumex::DescriptorSetLayout>(layoutBindings);
    pipelineLayout         = std::make_shared<pumex::PipelineLayout>();
    pipelineLayout->descriptorSetLayouts.push_back(descriptorSetLayout);
    descriptorPool         = std::make_shared<pumex::DescriptorPool>(2, layoutBindings);
    pipeline               = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, pipelineLayout, defaultRenderPass, 0);
    pipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/voxelizer_basic.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/voxelizer_basic.frag.spv")), "main" }
    };
    pipeline->vertexInput  =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
    };
    pipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    pipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    // build uniform buffers for camera
    cameraUbo         = std::make_shared<pumex::UniformBufferPerSurface<pumex::Camera>>(buffersAllocator);
    voxelizeCameraUbo = std::make_shared<pumex::UniformBufferPerSurface<pumex::Camera>>(buffersAllocator);

    // is this the fastest way to calculate all global transformations for a model ?
    std::vector<glm::mat4> globalTransforms = pumex::calculateResetPosition(*asset);
    PositionData modelData;
    std::copy(globalTransforms.begin(), globalTransforms.end(), std::begin(modelData.bones));
    positionUbo = std::make_shared<pumex::UniformBuffer<PositionData>>(modelData, buffersAllocator);
    PositionData vbData;
    voxelPositionUbo = std::make_shared<pumex::UniformBuffer<PositionData>>(vbData, buffersAllocator);

    voxelizeDescriptorSet = std::make_shared<pumex::DescriptorSet>(voxelizeDescriptorSetLayout, voxelizeDescriptorPool);
    voxelizeDescriptorSet->setDescriptor(0, voxelizeCameraUbo);
    voxelizeDescriptorSet->setDescriptor(1, positionUbo);
    voxelizeDescriptorSet->setDescriptor(2, clipMap);

    raymarchDescriptorSet = std::make_shared<pumex::DescriptorSet>(raymarchDescriptorSetLayout, raymarchDescriptorPool);
    raymarchDescriptorSet->setDescriptor(0, cameraUbo);
    raymarchDescriptorSet->setDescriptor(1, voxelPositionUbo);
    raymarchDescriptorSet->setDescriptor(2, clipMap);

    descriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorSetLayout, descriptorPool);
    descriptorSet->setDescriptor(0, cameraUbo);
    descriptorSet->setDescriptor(1, positionUbo);

    updateData.cameraPosition              = glm::vec3(0.0f, 0.0f, 0.0f);
    updateData.cameraGeographicCoordinates = glm::vec2(0.0f, 0.0f);
    updateData.cameraDistance              = 1.0f;
    updateData.leftMouseKeyPressed         = false;
    updateData.rightMouseKeyPressed        = false;
    updateData.moveForward                 = false;
    updateData.moveBackward                = false;
    updateData.moveLeft                    = false;
    updateData.moveRight                   = false;
  }

  void surfaceSetup(std::shared_ptr<pumex::Surface> surface)
  {
    pumex::Device*      devicePtr      = surface->device.lock().get();
    pumex::CommandPool* commandPoolPtr = surface->commandPool.get();

    myCmdBuffer[surface.get()] = std::make_shared<pumex::CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, devicePtr, commandPoolPtr, surface->getImageCount());

    cameraUbo->validate(surface.get());
    positionUbo->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
    voxelPositionUbo->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
    voxelizeCameraUbo->validate(surface.get());

    // loading models
    assetBuffer->validate(devicePtr, commandPoolPtr, surface->presentationQueue);

    clipMap->validate(devicePtr, commandPoolPtr, surface->presentationQueue);

    pipelineCache->validate(devicePtr);

    voxelizeDescriptorSetLayout->validate(devicePtr);
    voxelizeDescriptorPool->validate(devicePtr);
    voxelizePipelineLayout->validate(devicePtr);
    voxelizePipeline->validate(devicePtr);

    voxelizeFrameBufferImages->validate(surface.get());
    voxelizeRenderPass->validate(devicePtr);
    voxelizeFrameBuffer->validate(surface.get());

    raymarchDescriptorSetLayout->validate(devicePtr);
    raymarchDescriptorPool->validate(devicePtr);
    raymarchPipelineLayout->validate(devicePtr);
    raymarchPipeline->validate(devicePtr);

    descriptorSetLayout->validate(devicePtr);
    descriptorPool->validate(devicePtr);
    pipelineLayout->validate(devicePtr);
    pipeline->validate(devicePtr);

    // preparing descriptor sets
    voxelizeDescriptorSet->validate(surface.get());
    raymarchDescriptorSet->validate(surface.get());
    descriptorSet->validate(surface.get());
  }

  void processInput(std::shared_ptr<pumex::Surface> surface)
  {
    std::shared_ptr<pumex::Window>  windowSh = surface->window.lock();

    std::vector<pumex::InputEvent> mouseEvents = windowSh->getInputEvents();
    glm::vec2 mouseMove = updateData.lastMousePos;
    for (const auto& m : mouseEvents)
    {
      switch (m.type)
      {
      case pumex::InputEvent::MOUSE_KEY_PRESSED:
        if (m.mouseButton == pumex::InputEvent::LEFT)
          updateData.leftMouseKeyPressed = true;
        if (m.mouseButton == pumex::InputEvent::RIGHT)
          updateData.rightMouseKeyPressed = true;
        mouseMove.x = m.x;
        mouseMove.y = m.y;
        updateData.lastMousePos = mouseMove;
        break;
      case pumex::InputEvent::MOUSE_KEY_RELEASED:
        if (m.mouseButton == pumex::InputEvent::LEFT)
          updateData.leftMouseKeyPressed = false;
        if (m.mouseButton == pumex::InputEvent::RIGHT)
          updateData.rightMouseKeyPressed = false;
        break;
      case pumex::InputEvent::MOUSE_MOVE:
        if (updateData.leftMouseKeyPressed || updateData.rightMouseKeyPressed)
        {
          mouseMove.x = m.x;
          mouseMove.y = m.y;
        }
        break;
      case pumex::InputEvent::KEYBOARD_KEY_PRESSED:
        switch(m.key)
        {
        case pumex::InputEvent::W: updateData.moveForward  = true; break;
        case pumex::InputEvent::S: updateData.moveBackward = true; break;
        case pumex::InputEvent::A: updateData.moveLeft     = true; break;
        case pumex::InputEvent::D: updateData.moveRight    = true; break;
        }
        break;
      case pumex::InputEvent::KEYBOARD_KEY_RELEASED:
        switch(m.key)
        {
        case pumex::InputEvent::W: updateData.moveForward  = false; break;
        case pumex::InputEvent::S: updateData.moveBackward = false; break;
        case pumex::InputEvent::A: updateData.moveLeft     = false; break;
        case pumex::InputEvent::D: updateData.moveRight    = false; break;
        }
        break;
      }
    }

    uint32_t updateIndex = viewer->getUpdateIndex();
    RenderData& uData = renderData[updateIndex];
    uData.prevCameraGeographicCoordinates = updateData.cameraGeographicCoordinates;
    uData.prevCameraDistance = updateData.cameraDistance;
    uData.prevCameraPosition = updateData.cameraPosition;

    if (updateData.leftMouseKeyPressed)
    {
      updateData.cameraGeographicCoordinates.x -= 100.0f*(mouseMove.x - updateData.lastMousePos.x);
      updateData.cameraGeographicCoordinates.y += 100.0f*(mouseMove.y - updateData.lastMousePos.y);
      while (updateData.cameraGeographicCoordinates.x < -180.0f)
        updateData.cameraGeographicCoordinates.x += 360.0f;
      while (updateData.cameraGeographicCoordinates.x>180.0f)
        updateData.cameraGeographicCoordinates.x -= 360.0f;
      updateData.cameraGeographicCoordinates.y = glm::clamp(updateData.cameraGeographicCoordinates.y, -90.0f, 90.0f);
      updateData.lastMousePos = mouseMove;
    }
    if (updateData.rightMouseKeyPressed)
    {
      updateData.cameraDistance += 10.0f*(updateData.lastMousePos.y - mouseMove.y);
      if (updateData.cameraDistance<0.1f)
        updateData.cameraDistance = 0.1f;
      updateData.lastMousePos = mouseMove;
    }

    glm::vec3 forward = glm::vec3(cos(updateData.cameraGeographicCoordinates.x * 3.1415f / 180.0f), sin(updateData.cameraGeographicCoordinates.x * 3.1415f / 180.0f), 0) * 0.2f;
    glm::vec3 right = glm::vec3(cos((updateData.cameraGeographicCoordinates.x + 90.0f) * 3.1415f / 180.0f), sin((updateData.cameraGeographicCoordinates.x + 90.0f) * 3.1415f / 180.0f), 0) * 0.2f;
    if (updateData.moveForward)
      updateData.cameraPosition -= forward;
    if (updateData.moveBackward)
      updateData.cameraPosition += forward;
    if (updateData.moveLeft)
      updateData.cameraPosition -= right;
    if (updateData.moveRight)
      updateData.cameraPosition += right;

    uData.cameraGeographicCoordinates = updateData.cameraGeographicCoordinates;
    uData.cameraDistance = updateData.cameraDistance;
    uData.cameraPosition = updateData.cameraPosition;
  }

  void update(double timeSinceStart, double updateStep)
  {
  }

  void prepareCameraForRendering(std::shared_ptr<pumex::Surface> surface)
  {
    uint32_t renderIndex = viewer->getRenderIndex();
    const RenderData& rData = renderData[renderIndex];

    float deltaTime = pumex::inSeconds(viewer->getRenderTimeDelta());
    float renderTime = pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()) + deltaTime;

    glm::vec3 relCam
    (
      rData.cameraDistance * cos(rData.cameraGeographicCoordinates.x * 3.1415f / 180.0f) * cos(rData.cameraGeographicCoordinates.y * 3.1415f / 180.0f),
      rData.cameraDistance * sin(rData.cameraGeographicCoordinates.x * 3.1415f / 180.0f) * cos(rData.cameraGeographicCoordinates.y * 3.1415f / 180.0f),
      rData.cameraDistance * sin(rData.cameraGeographicCoordinates.y * 3.1415f / 180.0f)
    );
    glm::vec3 prevRelCam
    (
      rData.prevCameraDistance * cos(rData.prevCameraGeographicCoordinates.x * 3.1415f / 180.0f) * cos(rData.prevCameraGeographicCoordinates.y * 3.1415f / 180.0f),
      rData.prevCameraDistance * sin(rData.prevCameraGeographicCoordinates.x * 3.1415f / 180.0f) * cos(rData.prevCameraGeographicCoordinates.y * 3.1415f / 180.0f),
      rData.prevCameraDistance * sin(rData.prevCameraGeographicCoordinates.y * 3.1415f / 180.0f)
    );
    glm::vec3 eye = relCam + rData.cameraPosition;
    glm::vec3 prevEye = prevRelCam + rData.prevCameraPosition;

    glm::vec3 realEye = eye + deltaTime * (eye - prevEye);
    glm::vec3 realCenter = rData.cameraPosition + deltaTime * (rData.cameraPosition - rData.prevCameraPosition);

    glm::mat4 viewMatrix = glm::lookAt(realEye, realCenter, glm::vec3(0, 0, 1));

    pumex::Camera camera;
    camera.setViewMatrix(viewMatrix);
    camera.setObserverPosition(realEye);
    camera.setTimeSinceStart(renderTime);

    uint32_t renderWidth  = surface->swapChainSize.width;
    uint32_t renderHeight = surface->swapChainSize.height;
    camera.setProjectionMatrix(glm::perspective(glm::radians(60.0f), (float)renderWidth / (float)renderHeight, 0.1f, 100000.0f));

    cameraUbo->set(surface.get(), camera);

    pumex::Camera voxelizeCamera;
    voxelizeCamera.setObserverPosition(realEye);
    voxelizeCamera.setTimeSinceStart(renderTime);
    // near and far values must be multiplied by -1
    voxelizeCamera.setProjectionMatrix(pumex::orthoGL(voxelBoundingBox.bbMin.x, voxelBoundingBox.bbMax.x, voxelBoundingBox.bbMin.y, voxelBoundingBox.bbMax.y, -1.0f*voxelBoundingBox.bbMin.z, -1.0f*voxelBoundingBox.bbMax.z),false);

    voxelizeCameraUbo->set(surface.get(), voxelizeCamera);
  }

  void prepareModelForRendering()
  {
    std::shared_ptr<pumex::Asset> assetX = assetBuffer->getAsset(modelTypeID, 0);
    if (!assetX->animations.empty())
    {

      uint32_t renderIndex = viewer->getRenderIndex();
      const RenderData& rData = renderData[renderIndex];

      float deltaTime = pumex::inSeconds(viewer->getRenderTimeDelta());
      float renderTime = pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()) + deltaTime;


      PositionData positionData;
      pumex::Animation& anim = assetX->animations[0];
      pumex::Skeleton& skel = assetX->skeleton;

      uint32_t numAnimChannels = anim.channels.size();
      uint32_t numSkelBones = skel.bones.size();

      std::vector<uint32_t> boneChannelMapping(numSkelBones);
      for (uint32_t boneIndex = 0; boneIndex < numSkelBones; ++boneIndex)
      {
        auto it = anim.invChannelNames.find(skel.boneNames[boneIndex]);
        boneChannelMapping[boneIndex] = (it != anim.invChannelNames.end()) ? it->second : UINT32_MAX;
      }

      std::vector<glm::mat4> localTransforms(MAX_BONES);
      std::vector<glm::mat4> globalTransforms(MAX_BONES);

      anim.calculateLocalTransforms(renderTime, localTransforms.data(), numAnimChannels);
      uint32_t bcVal = boneChannelMapping[0];
      glm::mat4 localCurrentTransform = (bcVal == UINT32_MAX) ? skel.bones[0].localTransformation : localTransforms[bcVal];
      globalTransforms[0] = skel.invGlobalTransform * localCurrentTransform;
      for (uint32_t boneIndex = 1; boneIndex < numSkelBones; ++boneIndex)
      {
        bcVal = boneChannelMapping[boneIndex];
        localCurrentTransform = (bcVal == UINT32_MAX) ? skel.bones[boneIndex].localTransformation : localTransforms[bcVal];
        globalTransforms[boneIndex] = globalTransforms[skel.bones[boneIndex].parentIndex] * localCurrentTransform;
      }
      for (uint32_t boneIndex = 0; boneIndex < numSkelBones; ++boneIndex)
        positionData.bones[boneIndex] = globalTransforms[boneIndex] * skel.bones[boneIndex].offsetMatrix;
      positionUbo->set(positionData);
    }

    PositionData voxelPositionData;
    voxelPositionData.position = glm::translate(glm::mat4(), glm::vec3(voxelBoundingBox.bbMin.x, voxelBoundingBox.bbMin.y, voxelBoundingBox.bbMin.z)) * 
                                 glm::scale(glm::mat4(), glm::vec3(voxelBoundingBox.bbMax.x - voxelBoundingBox.bbMin.x, voxelBoundingBox.bbMax.y - voxelBoundingBox.bbMin.y, voxelBoundingBox.bbMax.z - voxelBoundingBox.bbMin.z));
    voxelPositionUbo->set(voxelPositionData);
  }

  void draw(std::shared_ptr<pumex::Surface> surface)
  {
    pumex::Surface*     surfacePtr = surface.get();
    pumex::Device*      devicePtr = surface->device.lock().get();
    pumex::CommandPool* commandPoolPtr = surface->commandPool.get();
    unsigned long long  frameNumber = surface->viewer.lock()->getFrameNumber();
    uint32_t            activeIndex = frameNumber % 3;
    uint32_t            renderWidth = surface->swapChainSize.width;
    uint32_t            renderHeight = surface->swapChainSize.height;

    cameraUbo->validate(surfacePtr);
    positionUbo->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
    voxelizeCameraUbo->validate(surfacePtr);
    voxelPositionUbo->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
    clipMap->validate(devicePtr, commandPoolPtr, surface->presentationQueue);

    auto& currentCmdBuffer = myCmdBuffer[surfacePtr];
    currentCmdBuffer->setActiveIndex(activeIndex);

    if (currentCmdBuffer->isDirty(activeIndex))
    {
//      std::vector<pumex::DescriptorSetValue> clipMapBuffer;
//      clipMap->getDescriptorSetValues(devicePtr->device, 0, clipMapBuffer);

      currentCmdBuffer->cmdBegin();

//      currentCmdBuffer->setImageLayout(*clipMap->getHandleImage(devicePtr->device, 0), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
      pumex::Image* image = clipMap->getHandleImage(devicePtr->device, 0);
      VkImageSubresourceRange subRes{};
      subRes.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      subRes.baseMipLevel   = 0;
      subRes.levelCount     = 1;
      subRes.baseArrayLayer = 0;
      subRes.layerCount     = 1;
      std::vector<VkImageSubresourceRange> subResources;
      subResources.push_back(subRes);
      currentCmdBuffer->cmdClearColorImage(*image, VK_IMAGE_LAYOUT_GENERAL, pumex::makeColorClearValue(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)), subResources);

      std::vector<VkClearValue> voxelClearValues = { pumex::makeColorClearValue(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)) };
      currentCmdBuffer->cmdBeginRenderPass(surfacePtr, voxelizeRenderPass.get(), voxelizeFrameBuffer.get(), 0, pumex::makeVkRect2D(0, 0, CLIPMAP_TEXTURE_SIZE, CLIPMAP_TEXTURE_SIZE), voxelClearValues);
      currentCmdBuffer->cmdSetViewport(0, { pumex::makeViewport(0, 0, CLIPMAP_TEXTURE_SIZE, CLIPMAP_TEXTURE_SIZE, 0.0f, 1.0f) });
      currentCmdBuffer->cmdSetScissor(0, { pumex::makeVkRect2D(0, 0, CLIPMAP_TEXTURE_SIZE, CLIPMAP_TEXTURE_SIZE) });

      currentCmdBuffer->cmdBindPipeline(voxelizePipeline.get());
      currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePtr, voxelizePipelineLayout.get(), 0, voxelizeDescriptorSet.get());
      assetBuffer->cmdBindVertexIndexBuffer(devicePtr, currentCmdBuffer.get(), 1, 0);
      assetBuffer->cmdDrawObject(devicePtr, currentCmdBuffer.get(), 1, modelTypeID, 0, 50.0f);
      currentCmdBuffer->cmdEndRenderPass();

//      currentCmdBuffer->setImageLayout( *clipMap->getHandleImage(devicePtr->device,0), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      std::vector<VkClearValue> clearValues = { pumex::makeColorClearValue(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), pumex::makeDepthStencilClearValue(1.0f, 0) };
      currentCmdBuffer->cmdBeginRenderPass(surfacePtr, defaultRenderPass.get(), surface->frameBuffer.get(), surface->getImageIndex(), pumex::makeVkRect2D(0, 0, renderWidth, renderHeight), clearValues);
      currentCmdBuffer->cmdSetViewport(0, { pumex::makeViewport(0, 0, renderWidth, renderHeight, 0.0f, 1.0f) });
      currentCmdBuffer->cmdSetScissor(0, { pumex::makeVkRect2D(0, 0, renderWidth, renderHeight) });

      // voxel raymarching
      currentCmdBuffer->cmdBindPipeline(raymarchPipeline.get());
      currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePtr, raymarchPipelineLayout.get(), 0, raymarchDescriptorSet.get());
      assetBuffer->cmdBindVertexIndexBuffer(devicePtr, currentCmdBuffer.get(), 1, 0);
      assetBuffer->cmdDrawObject(devicePtr, currentCmdBuffer.get(), 1, voxelBoxTypeID, 0, 50.0f);

      // model render
      currentCmdBuffer->cmdBindPipeline(pipeline.get());
      currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePtr, pipelineLayout.get(), 0, descriptorSet.get());
      assetBuffer->cmdBindVertexIndexBuffer(devicePtr, currentCmdBuffer.get(), 1, 0);
      assetBuffer->cmdDrawObject(devicePtr, currentCmdBuffer.get(), 1, modelTypeID, 0, 50.0f);

      currentCmdBuffer->cmdEndRenderPass();
      currentCmdBuffer->cmdEnd();
    }
    currentCmdBuffer->queueSubmit(surface->presentationQueue, { surface->imageAvailableSemaphore }, { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }, { surface->renderCompleteSemaphore }, VK_NULL_HANDLE);
  }

  std::shared_ptr<pumex::Viewer> viewer;
  std::string modelName;
  std::string animationName;
  uint32_t    modelTypeID;
  uint32_t    voxelBoxTypeID;

  UpdateData                                           updateData;
  std::array<RenderData, 3>                            renderData;

  std::shared_ptr<pumex::DeviceMemoryAllocator>        buffersAllocator;
  std::shared_ptr<pumex::DeviceMemoryAllocator>        verticesAllocator;
  std::shared_ptr<pumex::DeviceMemoryAllocator>        clipmapAllocator;
  
  std::shared_ptr<pumex::UniformBufferPerSurface<pumex::Camera>> cameraUbo;
  std::shared_ptr<pumex::UniformBuffer<PositionData>>  positionUbo;
  std::shared_ptr<pumex::UniformBufferPerSurface<pumex::Camera>> voxelizeCameraUbo;
  std::shared_ptr<pumex::UniformBuffer<PositionData>>  voxelPositionUbo;
  pumex::BoundingBox                                   voxelBoundingBox;

  std::shared_ptr<pumex::AssetBuffer>         assetBuffer;
  
  std::shared_ptr<pumex::Clipmap3>            clipMap;
  
  std::shared_ptr<pumex::RenderPass>          defaultRenderPass;

  std::shared_ptr<pumex::PipelineCache>       pipelineCache;

  std::shared_ptr<pumex::DescriptorSetLayout> descriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>      pipelineLayout;
  std::shared_ptr<pumex::DescriptorPool>      descriptorPool;
  std::shared_ptr<pumex::GraphicsPipeline>    pipeline;
  std::shared_ptr<pumex::DescriptorSet>       descriptorSet;

  std::shared_ptr<pumex::DescriptorSetLayout> raymarchDescriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>      raymarchPipelineLayout;
  std::shared_ptr<pumex::DescriptorPool>      raymarchDescriptorPool;
  std::shared_ptr<pumex::GraphicsPipeline>    raymarchPipeline;
  std::shared_ptr<pumex::DescriptorSet>       raymarchDescriptorSet;

  std::shared_ptr<pumex::DescriptorSetLayout> voxelizeDescriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>      voxelizePipelineLayout;
  std::shared_ptr<pumex::DescriptorPool>      voxelizeDescriptorPool;
  std::shared_ptr<pumex::GraphicsPipeline>    voxelizePipeline;
  std::shared_ptr<pumex::DescriptorSet>       voxelizeDescriptorSet;

  std::shared_ptr<pumex::FrameBufferImages>   voxelizeFrameBufferImages;
  std::shared_ptr<pumex::RenderPass>          voxelizeRenderPass;
  std::shared_ptr<pumex::FrameBuffer>         voxelizeFrameBuffer;

  std::unordered_map<pumex::Surface*, std::shared_ptr<pumex::CommandBuffer>> myCmdBuffer;
};

int main( int argc, char * argv[] )
{
  SET_LOG_INFO;
  args::ArgumentParser         parser("pumex example : model voxelization and rendering");
  args::HelpFlag               help(parser, "help", "display this help menu", { 'h', "help" });
  args::Flag                   enableDebugging(parser, "debug", "enable Vulkan debugging", { 'd' });
  args::Flag                   useFullScreen(parser, "fullscreen", "create fullscreen window", { 'f' });
  args::ValueFlag<std::string> modelNameArg(parser, "model", "3D model filename", { 'm' });
  args::ValueFlag<std::string> animationNameArg(parser, "animation", "3D model with animation", { 'a' });
  try
  {
    parser.ParseCLI(argc, argv);
  }
  catch (args::Help)
  {
    LOG_ERROR << parser;
    FLUSH_LOG;
    return 0;
  }
  catch (args::ParseError e)
  {
    LOG_ERROR << e.what() << std::endl;
    LOG_ERROR << parser;
    FLUSH_LOG;
    return 1;
  }
  catch (args::ValidationError e)
  {
    LOG_ERROR << e.what() << std::endl;
    LOG_ERROR << parser;
    FLUSH_LOG;
    return 1;
  }
  if (!modelNameArg)
  {
    LOG_ERROR << "Model filename is not defined" << std::endl;
    FLUSH_LOG;
    return 1;
  }
  std::string modelFileName = args::get(modelNameArg);
  std::string animationFileName = args::get(animationNameArg);
  std::string windowName = "Pumex voxelizer : ";
  windowName += modelFileName;

  const std::vector<std::string> requestDebugLayers = { { "VK_LAYER_LUNARG_standard_validation" } };
  pumex::ViewerTraits viewerTraits{ "pumex voxelizer", enableDebugging, requestDebugLayers, 60 };
  viewerTraits.debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT;// | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;

  std::shared_ptr<pumex::Viewer> viewer;
  try
  {
    viewer = std::make_shared<pumex::Viewer>(viewerTraits);

    std::vector<pumex::QueueTraits> requestQueues = { pumex::QueueTraits{ VK_QUEUE_GRAPHICS_BIT , 0, { 0.75f } } };
    std::vector<const char*> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device = viewer->addDevice(0, requestQueues, requestDeviceExtensions);
    CHECK_LOG_THROW(!device->isValid(), "Cannot create logical device with requested parameters");

    pumex::WindowTraits windowTraits{ 0, 100, 100, 640, 480, useFullScreen ? pumex::WindowTraits::FULLSCREEN : pumex::WindowTraits::WINDOW, windowName };
    std::shared_ptr<pumex::Window> window = pumex::Window::createWindow(windowTraits);

    std::vector<pumex::FrameBufferImageDefinition> frameBufferDefinitions =
    {
      { pumex::atSurface, VK_FORMAT_B8G8R8A8_UNORM,    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,         VK_IMAGE_ASPECT_COLOR_BIT,                               VK_SAMPLE_COUNT_1_BIT },
      { pumex::atDepth,   VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, VK_SAMPLE_COUNT_1_BIT }
    };

    // allocate 16 MB for frame buffers ( actually only depth buffer will be allocated )
    std::shared_ptr<pumex::DeviceMemoryAllocator> frameBufferAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 16 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    std::shared_ptr<pumex::FrameBufferImages> frameBufferImages = std::make_shared<pumex::FrameBufferImages>(frameBufferDefinitions, frameBufferAllocator);

    std::vector<pumex::AttachmentDefinition> renderPassAttachments =
    {
      { 0, VK_FORMAT_B8G8R8A8_UNORM,    VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0 },
      { 1, VK_FORMAT_D24_UNORM_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,         0 }
    };

    std::vector<pumex::SubpassDefinition> renderPassSubpasses = 
    {
      {
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        {},
        { { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
        {},
        { 1, VK_IMAGE_LAYOUT_GENERAL },
        {},
        0
      }
    };
    std::vector<pumex::SubpassDependencyDefinition> renderPassDependencies;

    std::shared_ptr<pumex::RenderPass> renderPass = std::make_shared<pumex::RenderPass>(renderPassAttachments, renderPassSubpasses, renderPassDependencies);

    pumex::SurfaceTraits surfaceTraits{ 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, VK_PRESENT_MODE_MAILBOX_KHR, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    surfaceTraits.definePresentationQueue(pumex::QueueTraits{ VK_QUEUE_GRAPHICS_BIT, 0,{ 0.75f } });
    surfaceTraits.setDefaultRenderPass(renderPass);
    surfaceTraits.setFrameBufferImages(frameBufferImages);

    std::shared_ptr<VoxelizerApplicationData> applicationData = std::make_shared<VoxelizerApplicationData>(viewer, modelFileName, animationFileName);
    applicationData->defaultRenderPass = renderPass;
    applicationData->setup();

    std::shared_ptr<pumex::Surface> surface = viewer->addSurface(window, device, surfaceTraits);
    applicationData->surfaceSetup(surface);

    tbb::flow::continue_node< tbb::flow::continue_msg > update(viewer->updateGraph, [=](tbb::flow::continue_msg)
    {
      applicationData->processInput(surface);
      applicationData->update(pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()), pumex::inSeconds(viewer->getUpdateDuration()));
    });

    tbb::flow::make_edge(viewer->startUpdateGraph, update);
    tbb::flow::make_edge(update, viewer->endUpdateGraph);

    // Making the render graph.
    // This one is also "single threaded" ( look at the make_edge() calls ), but presents a method of connecting graph nodes.
    // Consider make_edge() in render graph :
    // viewer->startRenderGraph should point to all root nodes.
    // All leaf nodes should point to viewer->endRenderGraph.
    tbb::flow::continue_node< tbb::flow::continue_msg > prepareBuffers(viewer->renderGraph, [=](tbb::flow::continue_msg) { applicationData->prepareModelForRendering(); });
    tbb::flow::continue_node< tbb::flow::continue_msg > startSurfaceFrame(viewer->renderGraph, [=](tbb::flow::continue_msg) { applicationData->prepareCameraForRendering(surface); surface->beginFrame(); });
    tbb::flow::continue_node< tbb::flow::continue_msg > drawSurfaceFrame(viewer->renderGraph, [=](tbb::flow::continue_msg) { applicationData->draw(surface); });
    tbb::flow::continue_node< tbb::flow::continue_msg > endSurfaceFrame(viewer->renderGraph, [=](tbb::flow::continue_msg) { surface->endFrame(); });

    tbb::flow::make_edge(viewer->startRenderGraph, prepareBuffers);
    tbb::flow::make_edge(prepareBuffers, startSurfaceFrame);
    tbb::flow::make_edge(startSurfaceFrame, drawSurfaceFrame);
    tbb::flow::make_edge(drawSurfaceFrame, endSurfaceFrame);
    tbb::flow::make_edge(endSurfaceFrame, viewer->endRenderGraph);

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