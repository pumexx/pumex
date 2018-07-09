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
#include <args.hxx>

// pumexvoxelizer shows how to voxelize a model ( model may be animated ).
// This example is based on pumexviewer.
// Render workflow performs two render operations per frame :
// - model voxelization
// - rendering of original model and ray marching of voxelized model

const uint32_t MAX_BONES = 511;

const uint32_t CLIPMAP_TEXTURE_COUNT = 1;
const uint32_t CLIPMAP_TEXTURE_SIZE  = 32;

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
  VoxelizerApplicationData(std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator, std::shared_ptr<pumex::DeviceMemoryAllocator> volumeAllocator, std::shared_ptr<pumex::Asset> a)
    : asset{ a }
  {
    // build uniform buffers for camera
    cameraBuffer         = std::make_shared<pumex::Buffer<pumex::Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce, true);
    voxelizeCameraBuffer = std::make_shared<pumex::Buffer<pumex::Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce, true);
    positionData         = std::make_shared<PositionData>();
    std::vector<glm::mat4> globalTransforms = pumex::calculateResetPosition(*asset);
    std::copy(begin(globalTransforms), end(globalTransforms), std::begin(positionData->bones));
    positionBuffer       = std::make_shared<pumex::Buffer<PositionData>>(positionData, buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerDevice, pumex::swOnce);
    voxelPositionData    = std::make_shared<PositionData>();
    voxelPositionBuffer  = std::make_shared<pumex::Buffer<PositionData>>(voxelPositionData, buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerDevice, pumex::swOnce);

    // build 3D texture
    pumex::ImageTraits   volumeImageTraits( VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_B8G8R8A8_UNORM, { CLIPMAP_TEXTURE_SIZE, CLIPMAP_TEXTURE_SIZE , CLIPMAP_TEXTURE_SIZE }, 1, CLIPMAP_TEXTURE_COUNT, VK_SAMPLE_COUNT_1_BIT, false, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_TYPE_3D, VK_SHARING_MODE_EXCLUSIVE);
    volumeMemoryImage = std::make_shared<pumex::MemoryImage>(volumeImageTraits, volumeAllocator, VK_IMAGE_ASPECT_COLOR_BIT, pumex::pbPerSurface, pumex::swOnce);

    updateData.cameraPosition              = glm::vec3(0.0f, 0.0f, 0.0f);
    updateData.cameraGeographicCoordinates = glm::vec2(0.0f, 0.0f);
    updateData.cameraDistance              = 1.0f;
    updateData.leftMouseKeyPressed         = false;
    updateData.rightMouseKeyPressed        = false;
    updateData.moveForward                 = false;
    updateData.moveBackward                = false;
    updateData.moveLeft                    = false;
    updateData.moveRight                   = false;

    pumex::BoundingBox bbox;
    if (asset->animations.size() > 0)
      bbox = pumex::calculateBoundingBox(asset->skeleton, asset->animations[0], true);
    else
      bbox = pumex::calculateBoundingBox(*asset,1);
    voxelBoundingBox = bbox;// pumex::BoundingBox(glm::vec3(-10.0f, -10.0f, 0.0f), glm::vec3(10.0f, 10.0f, 20.0f));
  }

  void processInput(std::shared_ptr<pumex::Surface> surface)
  {
    std::shared_ptr<pumex::Viewer> viewer = surface->viewer.lock();
    std::shared_ptr<pumex::Window> window = surface->window.lock();

    std::vector<pumex::InputEvent> mouseEvents = window->getInputEvents();
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

    volumeMemoryImage->clearImage(surface.get(), glm::vec4(0.0f));
  }

  void update(double timeSinceStart, double updateStep)
  {
  }

  void prepareCameraForRendering(std::shared_ptr<pumex::Surface> surface)
  {
    auto viewer = surface->viewer.lock();
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

    cameraBuffer->setData(surface.get(), camera);

    pumex::Camera voxelizeCamera;
    voxelizeCamera.setObserverPosition(realEye);
    voxelizeCamera.setTimeSinceStart(renderTime);
    // near and far values must be multiplied by -1
    voxelizeCamera.setProjectionMatrix(pumex::orthoGL(voxelBoundingBox.bbMin.x, voxelBoundingBox.bbMax.x, voxelBoundingBox.bbMin.y, voxelBoundingBox.bbMax.y, -1.0f*voxelBoundingBox.bbMin.z, -1.0f*voxelBoundingBox.bbMax.z),false);

    voxelizeCameraBuffer->setData(surface.get(), voxelizeCamera);
  }

  void prepareModelForRendering( std::shared_ptr<pumex::Viewer> viewer )
  {
    if (!asset->animations.empty())
    {
      uint32_t renderIndex = viewer->getRenderIndex();
      const RenderData& rData = renderData[renderIndex];

      float deltaTime = pumex::inSeconds(viewer->getRenderTimeDelta());
      float renderTime = pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()) + deltaTime;

      pumex::Animation& anim = asset->animations[0];
      pumex::Skeleton& skel = asset->skeleton;

      uint32_t numAnimChannels = anim.channels.size();
      uint32_t numSkelBones = skel.bones.size();

      std::vector<uint32_t> boneChannelMapping(numSkelBones);
      for (uint32_t boneIndex = 0; boneIndex < numSkelBones; ++boneIndex)
      {
        auto it = anim.invChannelNames.find(skel.boneNames[boneIndex]);
        boneChannelMapping[boneIndex] = (it != end(anim.invChannelNames)) ? it->second : UINT32_MAX;
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
        positionData->bones[boneIndex] = globalTransforms[boneIndex] * skel.bones[boneIndex].offsetMatrix;
      positionBuffer->invalidateData();
    }

    voxelPositionData->position = glm::translate(glm::mat4(), glm::vec3(voxelBoundingBox.bbMin.x, voxelBoundingBox.bbMin.y, voxelBoundingBox.bbMin.z)) * 
                                 glm::scale(glm::mat4(), glm::vec3(voxelBoundingBox.bbMax.x - voxelBoundingBox.bbMin.x, voxelBoundingBox.bbMax.y - voxelBoundingBox.bbMin.y, voxelBoundingBox.bbMax.z - voxelBoundingBox.bbMin.z));
    voxelPositionBuffer->invalidateData();
  }

  std::string modelName;
  std::string animationName;
  uint32_t    modelTypeID;
  uint32_t    voxelBoxTypeID;

  UpdateData                                           updateData;
  std::array<RenderData, 3>                            renderData;

  std::shared_ptr<pumex::Asset>                        asset;

  std::shared_ptr<pumex::Buffer<pumex::Camera>>        cameraBuffer;
  std::shared_ptr<PositionData>                        positionData;
  std::shared_ptr<pumex::Buffer<PositionData>>         positionBuffer;
  std::shared_ptr<pumex::Buffer<pumex::Camera>>        voxelizeCameraBuffer;
  std::shared_ptr<PositionData>                        voxelPositionData;
  std::shared_ptr<pumex::Buffer<PositionData>>         voxelPositionBuffer;
  std::shared_ptr<pumex::MemoryImage>                  volumeMemoryImage;

  pumex::BoundingBox                                   voxelBoundingBox;
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

  std::vector<std::string> instanceExtensions;
  std::vector<std::string> requestDebugLayers;
  if(enableDebugging)
    requestDebugLayers.push_back( "VK_LAYER_LUNARG_standard_validation" );
  pumex::ViewerTraits viewerTraits{ "pumex voxelizer", instanceExtensions, requestDebugLayers, 60 };
  viewerTraits.debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT;// | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;

  std::shared_ptr<pumex::Viewer> viewer;
  try
  {
    viewer = std::make_shared<pumex::Viewer>(viewerTraits);

    std::vector<pumex::VertexSemantic> requiredSemantic = { { pumex::VertexSemantic::Position, 3 },{ pumex::VertexSemantic::Normal, 3 },{ pumex::VertexSemantic::TexCoord, 2 },{ pumex::VertexSemantic::BoneWeight, 4 },{ pumex::VertexSemantic::BoneIndex, 4 } };
    pumex::AssetLoaderAssimp loader;
    std::shared_ptr<pumex::Asset> asset(loader.load(viewer, modelFileName, false, requiredSemantic));
    CHECK_LOG_THROW (asset.get() == nullptr,  "Model not loaded : " << modelFileName);
    if (!animationFileName.empty() )
    {
      std::shared_ptr<pumex::Asset> animAsset(loader.load(viewer, animationFileName, true, requiredSemantic));
      asset->animations = animAsset->animations;
    }

    std::vector<std::string> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device = viewer->addDevice(0, requestDeviceExtensions);

    pumex::WindowTraits windowTraits{ 0, 100, 100, 640, 480, useFullScreen ? pumex::WindowTraits::FULLSCREEN : pumex::WindowTraits::WINDOW, windowName };
    std::shared_ptr<pumex::Window> window = pumex::Window::createWindow(windowTraits);

    pumex::SurfaceTraits surfaceTraits{ 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, VK_PRESENT_MODE_MAILBOX_KHR, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    std::shared_ptr<pumex::Surface> surface = viewer->addSurface(window, device, surfaceTraits);

    // allocate 16 MB for frame buffer
    auto frameBufferAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 16 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // alocate 1 MB for uniform and storage buffers
    auto buffersAllocator     = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 64 MB for vertex and index buffers
    auto verticesAllocator    = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 64 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate memory for 3D texture
    auto volumeAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, CLIPMAP_TEXTURE_COUNT * CLIPMAP_TEXTURE_SIZE * CLIPMAP_TEXTURE_SIZE * CLIPMAP_TEXTURE_SIZE * 4 * 2, pumex::DeviceMemoryAllocator::FIRST_FIT);

    std::vector<pumex::QueueTraits> queueTraits{ { VK_QUEUE_GRAPHICS_BIT, 0, 0.75f } };

    std::shared_ptr<pumex::RenderWorkflow> workflow = std::make_shared<pumex::RenderWorkflow>("voxelizer_workflow", frameBufferAllocator, queueTraits);
      workflow->addResourceType("voxel_space",   false, VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, pumex::atColor,   pumex::AttachmentSize{ pumex::AttachmentSize::Absolute,         glm::vec2(CLIPMAP_TEXTURE_SIZE,CLIPMAP_TEXTURE_SIZE) }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
      workflow->addResourceType("depth_samples", false, VK_FORMAT_D32_SFLOAT,     VK_SAMPLE_COUNT_1_BIT, pumex::atDepth,   pumex::AttachmentSize{ pumex::AttachmentSize::SurfaceDependent, glm::vec2(1.0f,1.0f) }, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
      workflow->addResourceType("surface",       true,  VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, pumex::atSurface, pumex::AttachmentSize{ pumex::AttachmentSize::SurfaceDependent, glm::vec2(1.0f,1.0f) }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
      workflow->addResourceType("image_3d",      false, pumex::RenderWorkflowResourceType::Image);

    // first operation creates 3D texture of underlying model ( model voxelization )
    workflow->addRenderOperation("voxelization", pumex::RenderOperation::Graphics, 0, pumex::AttachmentSize( pumex::AttachmentSize::Absolute, glm::vec2(CLIPMAP_TEXTURE_SIZE,CLIPMAP_TEXTURE_SIZE)));
      workflow->addAttachmentOutput("voxelization", "voxel_space", "false_image", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, pumex::loadOpClear(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)));
      workflow->addImageOutput("voxelization", "image_3d", "voxels", VK_IMAGE_LAYOUT_GENERAL, pumex::loadOpClear(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)));

    // second operation renders 3D model and raymarches 3D texture to show that model and texture are in the same position
    workflow->addRenderOperation("rendering", pumex::RenderOperation::Graphics);
      workflow->addImageInput("rendering", "image_3d", "voxels", VK_IMAGE_LAYOUT_GENERAL);
      workflow->addAttachmentDepthOutput( "rendering", "depth_samples", "depth", VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, pumex::loadOpClear(glm::vec2(1.0f, 0.0f)));
      workflow->addAttachmentOutput(      "rendering", "surface",       "color", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,         pumex::loadOpClear(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)));

    std::shared_ptr<VoxelizerApplicationData> applicationData = std::make_shared<VoxelizerApplicationData>(buffersAllocator, volumeAllocator, asset);

    // memory objects that are not attachments must be provided to workflow through associateMemoryObject()
    // if they're not provided then there are no pipeline barriers for them
    workflow->associateMemoryObject("voxels", applicationData->volumeMemoryImage);

    // create pipeline cache
    auto pipelineCache = std::make_shared<pumex::PipelineCache>();

    // create pipeline for voxelization
    std::vector<pumex::DescriptorSetLayoutBinding> voxelizeLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    auto voxelizeDescriptorSetLayout   = std::make_shared<pumex::DescriptorSetLayout>(voxelizeLayoutBindings);
    auto voxelizePipelineLayout        = std::make_shared<pumex::PipelineLayout>();
    voxelizePipelineLayout->descriptorSetLayouts.push_back(voxelizeDescriptorSetLayout);
    auto voxelizePipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, voxelizePipelineLayout);
    voxelizePipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
    };
    voxelizePipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewer, "shaders/voxelizer_voxelize.vert.spv"), "main" },
      { VK_SHADER_STAGE_GEOMETRY_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/voxelizer_voxelize.geom.spv"), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/voxelizer_voxelize.frag.spv"), "main" }
    };
    voxelizePipeline->cullMode         = VK_CULL_MODE_NONE;
    voxelizePipeline->depthTestEnable  = VK_FALSE;
    voxelizePipeline->depthWriteEnable = VK_FALSE;
    workflow->setRenderOperationNode("voxelization", voxelizePipeline);

    auto voxelizeGroup = std::make_shared<pumex::Group>();
    voxelizeGroup->setName("voxelizeGroup");
    voxelizePipeline->addChild(voxelizeGroup);

    auto cameraUbo = std::make_shared<pumex::UniformBuffer>(applicationData->cameraBuffer);
    auto positionUbo = std::make_shared<pumex::UniformBuffer>(applicationData->positionBuffer);

    auto volumeImageView = std::make_shared<pumex::ImageView>(applicationData->volumeMemoryImage, applicationData->volumeMemoryImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_3D);
    auto volumeStorageImage = std::make_shared<pumex::StorageImage>(volumeImageView);

    auto voxelizeDescriptorSet = std::make_shared<pumex::DescriptorSet>(voxelizeDescriptorSetLayout);
    voxelizeDescriptorSet->setDescriptor(0, std::make_shared<pumex::UniformBuffer>(applicationData->voxelizeCameraBuffer));
    voxelizeDescriptorSet->setDescriptor(1, positionUbo);
    voxelizeDescriptorSet->setDescriptor(2, volumeStorageImage);
    voxelizeGroup->setDescriptorSet(0, voxelizeDescriptorSet);

    std::shared_ptr<pumex::AssetNode> assetNode = std::make_shared<pumex::AssetNode>(asset, verticesAllocator, 1, 0);
    assetNode->setName("assetNode");
    voxelizeGroup->addChild(assetNode);

    auto renderRoot = std::make_shared<pumex::Group>();
    renderRoot->setName("renderRoot");
    workflow->setRenderOperationNode("rendering", renderRoot);

    pumex::Geometry voxelBox;
    voxelBox.name = "voxelBox";
    voxelBox.semantic = requiredSemantic;
    voxelBox.materialIndex = 0;
    pumex::addBox(voxelBox, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), false);
    std::shared_ptr<pumex::Asset> voxelBoxAsset(pumex::createSimpleAsset(voxelBox, "voxelBox"));

    // create pipeline for ray marching
    std::vector<pumex::DescriptorSetLayoutBinding> raymarchLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
      { 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    auto raymarchDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(raymarchLayoutBindings);
    auto raymarchPipelineLayout      = std::make_shared<pumex::PipelineLayout>();
    raymarchPipelineLayout->descriptorSetLayouts.push_back(raymarchDescriptorSetLayout);
    auto raymarchPipeline            = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, raymarchPipelineLayout);
    raymarchPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/voxelizer_raymarch.vert.spv"), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/voxelizer_raymarch.frag.spv"), "main" }
    };
    raymarchPipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, voxelBoxAsset->geometries[0].semantic }
    };
    raymarchPipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    renderRoot->addChild(raymarchPipeline);

    std::shared_ptr<pumex::AssetNode> vbaAssetNode = std::make_shared<pumex::AssetNode>(voxelBoxAsset, verticesAllocator, 1, 0);
    vbaAssetNode->setName("vbaAssetNode");
    raymarchPipeline->addChild(vbaAssetNode);

    auto raymarchDescriptorSet = std::make_shared<pumex::DescriptorSet>(raymarchDescriptorSetLayout);
    raymarchDescriptorSet->setDescriptor(0, cameraUbo);
    raymarchDescriptorSet->setDescriptor(1, std::make_shared<pumex::UniformBuffer>(applicationData->voxelPositionBuffer));
    raymarchDescriptorSet->setDescriptor(2, volumeStorageImage);
    vbaAssetNode->setDescriptorSet(0, raymarchDescriptorSet);

    // create pipeline for basic model rendering
    std::vector<pumex::DescriptorSetLayoutBinding> layoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
    };
    auto descriptorSetLayout    = std::make_shared<pumex::DescriptorSetLayout>(layoutBindings);
    auto pipelineLayout         = std::make_shared<pumex::PipelineLayout>();
    pipelineLayout->descriptorSetLayouts.push_back(descriptorSetLayout);
    auto pipeline               = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, pipelineLayout);
    pipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/voxelizer_basic.vert.spv"), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/voxelizer_basic.frag.spv"), "main" }
    };
    pipeline->vertexInput  =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
    };
    pipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    renderRoot->addChild(pipeline);

    auto renderGroup = std::make_shared<pumex::Group>();
    renderGroup->setName("renderGroup");
    pipeline->addChild(renderGroup);

    auto descriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorSetLayout);
    descriptorSet->setDescriptor(0, cameraUbo);
    descriptorSet->setDescriptor(1, positionUbo);
    renderGroup->setDescriptorSet(0, descriptorSet);

    renderGroup->addChild(assetNode);

    std::shared_ptr<pumex::SingleQueueWorkflowCompiler> workflowCompiler = std::make_shared<pumex::SingleQueueWorkflowCompiler>();
    surface->setRenderWorkflow(workflow, workflowCompiler);

    tbb::flow::continue_node< tbb::flow::continue_msg > update(viewer->updateGraph, [=](tbb::flow::continue_msg)
    {
      applicationData->processInput(surface);
      applicationData->update(pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()), pumex::inSeconds(viewer->getUpdateDuration()));
    });

    tbb::flow::make_edge(viewer->opStartUpdateGraph, update);
    tbb::flow::make_edge(update, viewer->opEndUpdateGraph);

    viewer->setEventRenderStart(std::bind(&VoxelizerApplicationData::prepareModelForRendering, applicationData, std::placeholders::_1));

    surface->setEventSurfaceRenderStart(std::bind(&VoxelizerApplicationData::prepareCameraForRendering, applicationData, std::placeholders::_1));

    viewer->run();
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
  viewer->cleanup();
  FLUSH_LOG;
  return 0;
}
