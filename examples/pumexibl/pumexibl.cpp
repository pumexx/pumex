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
#include <pumex/Pumex.h>
#include <pumex/AssetLoaderAssimp.h>
#include <pumex/utils/Shapes.h>
#include <args.hxx>

// pumexibl presents how to render to mipmaps and array layers and how to use Image Based Lighting

const uint32_t MAX_BONES = 511;
const uint32_t PREFILTERED_ENVIRONMENT_MIPMAPS = 6;

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

struct ViewerApplicationData
{
  ViewerApplicationData( std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator )
  {
    // create buffers visible from renderer
    cameraBuffer     = std::make_shared<pumex::Buffer<pumex::Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce, true);
    textCameraBuffer = std::make_shared<pumex::Buffer<pumex::Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce, true);
    positionData     = std::make_shared<PositionData>();
    positionBuffer   = std::make_shared<pumex::Buffer<PositionData>>(positionData, buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerDevice, pumex::swOnce);
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

  void prepareModelForRendering(pumex::Viewer* viewer, std::shared_ptr<pumex::Asset> asset)
  {
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

  std::shared_ptr<pumex::Buffer<pumex::Camera>> cameraBuffer;
  std::shared_ptr<pumex::Buffer<pumex::Camera>> textCameraBuffer;
  std::shared_ptr<PositionData>                 positionData;
  std::shared_ptr<pumex::Buffer<PositionData>>  positionBuffer;
  std::shared_ptr<pumex::BasicCameraHandler>    camHandler;
};

int main( int argc, char * argv[] )
{
  SET_LOG_INFO;

  // process command line using args library
  args::ArgumentParser                         parser("pumex example : Image Based Lighting and Physically Based Rendering");
  args::HelpFlag                               help(parser, "help", "display this help menu", { 'h', "help" });
  args::Flag                                   enableDebugging(parser, "debug", "enable Vulkan debugging", { 'd' });
  args::Flag                                   useFullScreen(parser, "fullscreen", "create fullscreen window", { 'f' });
  args::MapFlag<std::string, VkPresentModeKHR> presentationMode(parser, "presentation_mode", "presentation mode (immediate, mailbox, fifo, fifo_relaxed)", { 'p' }, pumex::Surface::nameToPresentationModes, VK_PRESENT_MODE_MAILBOX_KHR);
  args::ValueFlag<uint32_t>                    updatesPerSecond(parser, "update_frequency", "number of update calls per second", { 'u' }, 60);
  args::ValueFlag<std::string>                 equirectangularImageName(parser, "equirectangular_image", "equirectangular image filename", { 'i' }, "ibl/syferfontein_0d_clear_2k.dds");
  args::Positional<std::string>                modelNameArg(parser, "model", "3D model filename");
  args::Positional<std::string>                animationNameArg(parser, "animation", "3D model with animation");
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
  VkPresentModeKHR presentMode        = args::get(presentationMode);
  uint32_t updateFrequency            = std::max(1U, args::get(updatesPerSecond));
  std::string equirectangularFileName = args::get(equirectangularImageName);
  std::string modelFileName           = args::get(modelNameArg);
  std::string animationFileName       = args::get(animationNameArg);
  std::string windowName              = "Pumex viewer : ";
  windowName += modelFileName;

  // We need to prepare ViewerTraits object. It stores all basic configuration for Vulkan instance ( Viewer class )
  std::vector<std::string> instanceExtensions;
  std::vector<std::string> requestDebugLayers;
  if (enableDebugging)
    requestDebugLayers.push_back("VK_LAYER_LUNARG_standard_validation");
  pumex::ViewerTraits viewerTraits{ "pumex viewer", instanceExtensions, requestDebugLayers, updateFrequency };
  viewerTraits.debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT;// | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;

  std::shared_ptr<pumex::Viewer> viewer;
  try
  {
    viewer = std::make_shared<pumex::Viewer>(viewerTraits);
    // alocate 256 MB for frame buffers and create viewer
    std::shared_ptr<pumex::DeviceMemoryAllocator> frameBufferAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 256 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    viewer->setFrameBufferAllocator(frameBufferAllocator);

    // vertex semantic defines how a single vertex in an asset will look like
    std::vector<pumex::VertexSemantic> requiredSemantic = { { pumex::VertexSemantic::Position, 3 },{ pumex::VertexSemantic::Normal, 3 },{ pumex::VertexSemantic::Tangent, 3 },{ pumex::VertexSemantic::TexCoord, 2 },{ pumex::VertexSemantic::BoneWeight, 4 },{ pumex::VertexSemantic::BoneIndex, 4 } };

    // we load an asset using Assimp asset loader
    pumex::AssetLoaderAssimp loader;
    loader.setImportFlags(loader.getImportFlags() | aiProcess_CalcTangentSpace);
    std::shared_ptr<pumex::Asset> asset(loader.load(viewer, modelFileName, false, requiredSemantic));

    if (!animationFileName.empty() )
    {
      std::shared_ptr<pumex::Asset> animAsset(loader.load(viewer, animationFileName, true, requiredSemantic));
      asset->animations = animAsset->animations;
    }

    auto fullEquirectangularFileName = viewer->getAbsoluteFilePath(equirectangularFileName);
    CHECK_LOG_THROW(fullEquirectangularFileName.empty(), "Cannot find equirectangular texture : " << equirectangularFileName);
    auto equirectangularTexture = std::make_shared<gli::texture>(gli::load(fullEquirectangularFileName));
    CHECK_LOG_THROW(equirectangularTexture == nullptr, "Cannot load equirectangular texture : " << equirectangularFileName);

    // now is the time to create devices, windows and surfaces.
    std::vector<std::string> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device = viewer->addDevice(0, requestDeviceExtensions);

    // window traits define the screen on which the window will be shown, coordinates on that window, etc
    pumex::WindowTraits windowTraits{ 0, 100, 100, 640, 480, useFullScreen ? pumex::WindowTraits::FULLSCREEN : pumex::WindowTraits::WINDOW, windowName, true };
    std::shared_ptr<pumex::Window> window = pumex::Window::createNativeWindow(windowTraits);

    pumex::ResourceDefinition swapChainDefinition = pumex::SWAPCHAIN_DEFINITION(VK_FORMAT_B8G8R8A8_UNORM);
    pumex::SurfaceTraits surfaceTraits{ swapChainDefinition, 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, presentMode, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    std::shared_ptr<pumex::Surface> surface = window->createSurface(device, surfaceTraits);

    // alocate 1 MB for uniform and storage buffers
    std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 8 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 64 MB for vertex and index buffers
    std::shared_ptr<pumex::DeviceMemoryAllocator> verticesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 64 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 32 MB memory for font textures and environment texture
    std::shared_ptr<pumex::DeviceMemoryAllocator> texturesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 64 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // create common descriptor pool
    std::shared_ptr<pumex::DescriptorPool> descriptorPool = std::make_shared<pumex::DescriptorPool>();

    std::shared_ptr<pumex::RenderGraph> prepareIblRenderGraph = std::make_shared<pumex::RenderGraph>("prepare_ibl_render_graph");

    pumex::ImageSize           environmentCubeMapSize{ pumex::isAbsolute, glm::vec2(1024.0f,1024.0f), 6, 1, 1 };
    pumex::ImageSize           irradianceCubeMapSize{ pumex::isAbsolute, glm::vec2(32.0f,32.0f), 6, 1, 1 };
    pumex::ImageSize           prefilteredEnvironmentCubeMapSize{ pumex::isAbsolute, glm::vec2(1024.0f,1024.0f), 6, PREFILTERED_ENVIRONMENT_MIPMAPS, 1 };
    pumex::ImageSize           brdfTextureSize{ pumex::isAbsolute, glm::vec2(512.0f,512.0f), 1, 1, 1 };
    pumex::ImageSize           cubeMapRenderSize{ pumex::isAbsolute, glm::vec2(1024.0f,1024.0f), 1, 1, 1 };
    pumex::ImageSize           irradianceRenderSize{ pumex::isAbsolute, glm::vec2(32.0f,32.0f), 1, 1, 1 };

    pumex::ResourceDefinition  environmentCubeMapDefinition{ VK_FORMAT_R32G32B32_SFLOAT, environmentCubeMapSize, pumex::atColor };
    pumex::ResourceDefinition  irradianceCubeMapDefinition{ VK_FORMAT_R32G32B32_SFLOAT, irradianceCubeMapSize, pumex::atColor };
    pumex::ResourceDefinition  prefilteredEnvironmentCubeMapDefinition{ VK_FORMAT_R32G32B32_SFLOAT, prefilteredEnvironmentCubeMapSize, pumex::atColor };
    pumex::ResourceDefinition  brdfDefinition{ VK_FORMAT_R32G32B32_SFLOAT, brdfTextureSize, pumex::atColor };

    pumex::LoadOp cubeMapClear = pumex::loadOpClear(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    // first batch of operations converts equirectangular map to a cubemap
    for(uint32_t i=0; i<6; ++i)
    {
      std::stringstream str;
      str<<"eqr_"<<i;
      pumex::RenderOperation cubeMapRender(str.str(), pumex::opGraphics, cubeMapRenderSize);
        cubeMapRender.addAttachmentOutput("face", environmentCubeMapDefinition, cubeMapClear, pumex::ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, i, 1), VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, true);
      prepareIblRenderGraph->addRenderOperation(cubeMapRender);
    }

    // second batch of operations creates diffuse irradiance map
    for (uint32_t i = 0; i<6; ++i)
    {
      std::stringstream str;
      str << "irr_" << i;
      pumex::RenderOperation irradianceRender(str.str(), pumex::opGraphics, irradianceRenderSize);
        irradianceRender.addImageInput("cubemap_in", environmentCubeMapDefinition, pumex::loadOpDontCare(), pumex::ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
        irradianceRender.addAttachmentOutput("face", irradianceCubeMapDefinition, cubeMapClear, pumex::ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, i, 1), VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, true);
      prepareIblRenderGraph->addRenderOperation(irradianceRender);
    }

    // third batch of operations creates prefiltered environment map for specular IBL reflections
    // here we are rendering not only to cubemap faces, but also to its mipmaps
    for (uint32_t j = 0; j < PREFILTERED_ENVIRONMENT_MIPMAPS; ++j)
    {
      for (uint32_t i = 0; i<6; ++i)
      {
        std::stringstream str;
        str << "per_" << i << "_" << j;

        uint32_t jj = pow(2, j);
        pumex::ImageSize prefilteredEnvironmentRenderSize{ pumex::isAbsolute, glm::vec2(1024.0f/jj, 1024.0f / jj), 1, 1, 1 };
        pumex::RenderOperation prefilteredRender(str.str(), pumex::opGraphics, prefilteredEnvironmentRenderSize);
          prefilteredRender.addImageInput("cubemap_in", environmentCubeMapDefinition, pumex::loadOpDontCare(), pumex::ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
          prefilteredRender.addAttachmentOutput("face_mip", prefilteredEnvironmentCubeMapDefinition, cubeMapClear, pumex::ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, j, 1, i, 1), VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, true);
        prepareIblRenderGraph->addRenderOperation(prefilteredRender);
      }
    }

    // next operation generates BRDF map
    pumex::RenderOperation brdfRender("brdf", pumex::opGraphics, brdfTextureSize);
      brdfRender.addAttachmentOutput("brdf_out", brdfDefinition, cubeMapClear, pumex::ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1), 0, true);
    prepareIblRenderGraph->addRenderOperation(brdfRender);

    // and finally - last operation renders model to screen using previously generated cubemaps to realize image based lighting
    pumex::ImageSize fullScreenSize{ pumex::isSurfaceDependent, glm::vec2(1.0f,1.0f) };
    pumex::ResourceDefinition depthSamples(VK_FORMAT_D32_SFLOAT, fullScreenSize, pumex::atDepth);

    pumex::RenderOperation rendering("rendering", pumex::opGraphics, fullScreenSize);
      rendering.addImageInput("irradiance_map",              irradianceCubeMapDefinition,             pumex::loadOpDontCare(), pumex::ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
      rendering.addImageInput("prefiltered_environment_map", prefilteredEnvironmentCubeMapDefinition, pumex::loadOpDontCare(), pumex::ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, PREFILTERED_ENVIRONMENT_MIPMAPS, 0, 6), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
      rendering.addImageInput("brdf_map",                    brdfDefinition,                          pumex::loadOpDontCare(), pumex::ImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0, VK_IMAGE_VIEW_TYPE_2D);
      rendering.setAttachmentDepthOutput("depth",            depthSamples,                            pumex::loadOpClear(glm::vec2(1.0f, 0.0f)));
      rendering.addAttachmentOutput(pumex::SWAPCHAIN_NAME,   swapChainDefinition,                     pumex::loadOpClear(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)));
    prepareIblRenderGraph->addRenderOperation(rendering);

    // operations are ready - time to add all required transitions between operations
    std::vector<pumex::ResourceTransitionDescription> transitions_1_2_3;
    std::vector<pumex::ResourceTransitionDescription> transitions_2_final;
    for (uint32_t i = 0; i < 6; ++i)
    {
      std::stringstream opGen;
      opGen << "eqr_" << i;
      for (uint32_t j = 0; j < 6; ++j)
      {
        std::stringstream opCon;
        opCon << "irr_" << j;
        transitions_1_2_3.push_back({ opGen.str(), "face", opCon.str(), "cubemap_in" });
        transitions_2_final.push_back({ opCon.str(), "face", "rendering", "irradiance_map" });
      }
    }

    // transitions between first batch and third batch
    std::vector<pumex::ResourceTransitionDescription> transitions_3_final;
    for (uint32_t i = 0; i<6; ++i)
    {
      std::stringstream opGen;
      opGen << "eqr_" << i;
      for (uint32_t k = 0; k < PREFILTERED_ENVIRONMENT_MIPMAPS; ++k)
      {
        for (uint32_t j = 0; j < 6; ++j)
        {
          std::stringstream opCon;
          opCon << "per_" << j << "_" << k;
          transitions_1_2_3.push_back({ opGen.str(), "face", opCon.str(), "cubemap_in" });
          transitions_3_final.push_back({ opCon.str(), "face_mip", "rendering", "prefiltered_environment_map" });
        }
      }
    }

    prepareIblRenderGraph->addResourceTransition(transitions_1_2_3);
    prepareIblRenderGraph->addResourceTransition(transitions_2_final);
    prepareIblRenderGraph->addResourceTransition(transitions_3_final);
    prepareIblRenderGraph->addResourceTransition("brdf", "brdf_out", "rendering", "brdf_map");

    // operations and transitions are ready - now we have to build scene graphs for each operation
    // First let's start with creating object used in most of the operations : cubemap camera parameters, input image sampler, sphere geometry, common pipeline cache, etc
    glm::mat4 cubeMapProjectionMatrix = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    std::vector<glm::mat4> cubeMapViewMatrices =
    {
      glm::lookAt( glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f),  glm::vec3(0.0f, -1.0f,  0.0f) ),
      glm::lookAt( glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f) ),
      glm::lookAt( glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f),  glm::vec3(0.0f,  0.0f,  1.0f) ),
      glm::lookAt( glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f),  glm::vec3(0.0f,  0.0f, -1.0f) ),
      glm::lookAt( glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f),  glm::vec3(0.0f, -1.0f,  0.0f) ),
      glm::lookAt( glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f),  glm::vec3(0.0f, -1.0f,  0.0f) )
    };
    std::vector<std::shared_ptr<pumex::UniformBuffer>> cubeMapCameraUbos;
    for (uint32_t i = 0; i < 6; ++i)
    {
      auto cubeMapCamera       = std::make_shared<pumex::Camera>(cubeMapViewMatrices[i], cubeMapProjectionMatrix, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.0f);
      auto cubeMapCameraBuffer = std::make_shared<pumex::Buffer<pumex::Camera>>(cubeMapCamera, buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerDevice, pumex::swOnce);
      cubeMapCameraUbos.push_back(std::make_shared<pumex::UniformBuffer>(cubeMapCameraBuffer));
    }

    // building sampler for input equirectangular image
    auto equirectangularImage     = std::make_shared<pumex::MemoryImage>(equirectangularTexture, texturesAllocator);
    auto equirectangularImageView = std::make_shared<pumex::ImageView>(equirectangularImage, equirectangularImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D );
    auto sampler                  = std::make_shared<pumex::Sampler>(pumex::SamplerTraits());
    auto equirectangularSampler   = std::make_shared<pumex::CombinedImageSampler>(equirectangularImageView, sampler);

    pumex::Geometry sphereGeometry;
    sphereGeometry.name     = "sphereGeometry";
    sphereGeometry.semantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::TexCoord, 2 } };
    pumex::addSphere(sphereGeometry, glm::vec3(0.0f, 0.0f, 0.0f), 1.0f, 36, 36, true);
    auto sphereAsset        = pumex::createSimpleAsset(sphereGeometry, "sphereAsset");

    std::shared_ptr<pumex::AssetNode> sphereAssetNode = std::make_shared<pumex::AssetNode>(sphereAsset, verticesAllocator, 1, 0);
    sphereAssetNode->setName("sphereAssetNode");

    auto pipelineCache = std::make_shared<pumex::PipelineCache>();

    // pipeline layout, descriptor set layout and shaders for first batch of operations ( converting equirectangular image to cubemap )
    std::vector<pumex::DescriptorSetLayoutBinding> eqrLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    auto eqrDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(eqrLayoutBindings);
    auto eqrPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    eqrPipelineLayout->descriptorSetLayouts.push_back(eqrDescriptorSetLayout);

    auto eqrVertexShader   = std::make_shared<pumex::ShaderModule>(viewer, "shaders/ibl_equirectangular_to_cubemap.vert.spv");
    auto eqrFragmentShader = std::make_shared<pumex::ShaderModule>(viewer, "shaders/ibl_equirectangular_to_cubemap.frag.spv");

    // scenegraphs for first batch of operations
    for (uint32_t i = 0; i < 6; ++i)
    {
      std::stringstream str;
      str << "eqr_" << i;

      auto eqrRoot = std::make_shared<pumex::Group>();
      eqrRoot->setName(str.str() + "_root");
      prepareIblRenderGraph->setRenderOperationNode(str.str(), eqrRoot);

      auto eqrPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, eqrPipelineLayout);
      // loading vertex and fragment shader
      eqrPipeline->shaderStages =
      {
        { VK_SHADER_STAGE_VERTEX_BIT, eqrVertexShader, "main" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, eqrFragmentShader, "main" }
      };
      // vertex input - we will use the same vertex semantic that the loaded model has
      eqrPipeline->vertexInput =
      {
        { 0, VK_VERTEX_INPUT_RATE_VERTEX, sphereAsset->geometries[0].semantic }
      };
      eqrPipeline->depthTestEnable  = VK_FALSE;
      eqrPipeline->depthWriteEnable = VK_FALSE;
      eqrPipeline->blendAttachments =
      {
        { VK_FALSE, 0xF }
      };
      eqrRoot->addChild(eqrPipeline);

      auto eqrDescriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorPool, eqrDescriptorSetLayout);
      eqrDescriptorSet->setDescriptor(0, cubeMapCameraUbos[i]);
      eqrDescriptorSet->setDescriptor(1, equirectangularSampler);
      eqrPipeline->setDescriptorSet(0, eqrDescriptorSet);

      eqrPipeline->addChild(sphereAssetNode);
    }

    // pipeline layout, descriptor set layout and shaders for second batch of operations ( calculating diffuse irradiance )
    std::vector<pumex::DescriptorSetLayoutBinding> irrLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_VERTEX_BIT },
    { 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    auto irrDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(irrLayoutBindings);
    auto irrPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    irrPipelineLayout->descriptorSetLayouts.push_back(irrDescriptorSetLayout);

    auto irrVertexShader   = std::make_shared<pumex::ShaderModule>(viewer, "shaders/ibl_calculate_diffuse_irradiance.vert.spv");
    auto irrFragmentShader = std::make_shared<pumex::ShaderModule>(viewer, "shaders/ibl_calculate_diffuse_irradiance.frag.spv");

    auto irrCubeMapSampler = std::make_shared<pumex::CombinedImageSampler>("cubemap_in", sampler);

    // scenegraphs for second batch of operations ( diffuse irradiance )
    for (uint32_t i = 0; i < 6; ++i)
    {
      std::stringstream str;
      str << "irr_" << i;

      auto irrRoot = std::make_shared<pumex::Group>();
      irrRoot->setName(str.str() + "_root");
      prepareIblRenderGraph->setRenderOperationNode(str.str(), irrRoot);

      auto irrPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, irrPipelineLayout);
      // loading vertex and fragment shader
      irrPipeline->shaderStages =
      {
        { VK_SHADER_STAGE_VERTEX_BIT,   irrVertexShader,   "main" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, irrFragmentShader, "main" }
      };
      // vertex input - we will use the same vertex semantic that the loaded model has
      irrPipeline->vertexInput =
      {
        { 0, VK_VERTEX_INPUT_RATE_VERTEX, sphereAsset->geometries[0].semantic }
      };
      irrPipeline->depthTestEnable  = VK_FALSE;
      irrPipeline->depthWriteEnable = VK_FALSE;
      irrPipeline->blendAttachments =
      {
        { VK_FALSE, 0xF }
      };
      irrRoot->addChild(irrPipeline);

      auto irrDescriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorPool, irrDescriptorSetLayout);
      irrDescriptorSet->setDescriptor(0, cubeMapCameraUbos[i]);
      irrDescriptorSet->setDescriptor(1, irrCubeMapSampler);
      irrPipeline->setDescriptorSet(0, irrDescriptorSet);

      irrPipeline->addChild(sphereAssetNode);
    }

    // pipeline layout and descriptor set layout for third batch of operations ( calculating prefiltered environment map for specular higlights )
    std::vector<pumex::DescriptorSetLayoutBinding> perLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 2, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    auto perDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(perLayoutBindings);
    auto perPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    perPipelineLayout->descriptorSetLayouts.push_back(perDescriptorSetLayout);

    auto perVertexShader   = std::make_shared<pumex::ShaderModule>(viewer, "shaders/ibl_calculate_prefiltered_environment.vert.spv");
    auto perFragmentShader = std::make_shared<pumex::ShaderModule>(viewer, "shaders/ibl_calculate_prefiltered_environment.frag.spv");

    auto perCubeMapSampler = std::make_shared<pumex::CombinedImageSampler>("cubemap_in", sampler);

    std::vector<std::shared_ptr<pumex::UniformBuffer>> roughnessUbos;
    for (uint32_t j = 0; j < PREFILTERED_ENVIRONMENT_MIPMAPS; ++j)
    {
      auto roughness = std::make_shared<float>(0.0f);
      auto roughnessBuffer = std::make_shared<pumex::Buffer<float>>(roughness, buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerDevice, pumex::swOnce);
      roughnessUbos.push_back( std::make_shared<pumex::UniformBuffer>(roughnessBuffer) );
    }

    // scenegraphs for third batch of operations ( prefiltered environment )
    for (uint32_t j = 0; j < PREFILTERED_ENVIRONMENT_MIPMAPS; ++j)
    {
      for (uint32_t i = 0; i < 6; ++i)
      {
        std::stringstream str;
        str << "per_" << i << "_" << j;

        auto perRoot = std::make_shared<pumex::Group>();
        perRoot->setName(str.str() + "_root");
        prepareIblRenderGraph->setRenderOperationNode(str.str(), perRoot);

        auto perPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, perPipelineLayout);
        // loading vertex and fragment shader
        perPipeline->shaderStages =
        {
          { VK_SHADER_STAGE_VERTEX_BIT,   perVertexShader,   "main" },
          { VK_SHADER_STAGE_FRAGMENT_BIT, perFragmentShader, "main" }
        };
        // vertex input - we will use the same vertex semantic that the loaded model has
        perPipeline->vertexInput =
        {
          { 0, VK_VERTEX_INPUT_RATE_VERTEX, sphereAsset->geometries[0].semantic }
        };
        perPipeline->depthTestEnable = VK_FALSE;
        perPipeline->depthWriteEnable = VK_FALSE;
        perPipeline->blendAttachments =
        {
          { VK_FALSE, 0xF }
        };
        perRoot->addChild(perPipeline);

        auto perDescriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorPool, perDescriptorSetLayout);
        perDescriptorSet->setDescriptor(0, cubeMapCameraUbos[i]);
        perDescriptorSet->setDescriptor(1, perCubeMapSampler);
        perDescriptorSet->setDescriptor(2, roughnessUbos[j]);
        perPipeline->setDescriptorSet(0, perDescriptorSet);

        perPipeline->addChild(sphereAssetNode);
      }
    }

    // pipeline layout and descriptor set layout for last calculating operation ( calculating BRDF )
    std::vector<pumex::DescriptorSetLayoutBinding> brdfLayoutBindings =
    {
    };
    auto brdfDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(brdfLayoutBindings);
    auto brdfPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    brdfPipelineLayout->descriptorSetLayouts.push_back(brdfDescriptorSetLayout);

    auto brdfVertexShader = std::make_shared<pumex::ShaderModule>(viewer, "shaders/ibl_calculate_brdf.vert.spv");
    auto brdfFragmentShader = std::make_shared<pumex::ShaderModule>(viewer, "shaders/ibl_calculate_brdf.frag.spv");

    // scenegraphs for third batch of operations ( prefiltered environment )
    auto brdfRoot = std::make_shared<pumex::Group>();
    brdfRoot->setName("brdf_root");
    prepareIblRenderGraph->setRenderOperationNode("brdf", brdfRoot);

    std::shared_ptr<pumex::Asset> fullScreenTriangle = pumex::createFullScreenTriangle();

    auto brdfPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, brdfPipelineLayout);
    // loading vertex and fragment shader
    brdfPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   brdfVertexShader,   "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, brdfFragmentShader, "main" }
    };
    // vertex input - we will use the same vertex semantic that the loaded model has
    brdfPipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, fullScreenTriangle->geometries[0].semantic }
    };
    brdfPipeline->depthTestEnable = VK_FALSE;
    brdfPipeline->depthWriteEnable = VK_FALSE;
    brdfPipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    brdfRoot->addChild(brdfPipeline);

    std::shared_ptr<pumex::AssetNode> fullScreenAssetNode = std::make_shared<pumex::AssetNode>(fullScreenTriangle, verticesAllocator, 1, 0);
    fullScreenAssetNode->setName("fullScreenAssetNode");

    brdfPipeline->addChild(fullScreenAssetNode);

    // now we are building scene graph for :rendering" node
    auto renderRoot = std::make_shared<pumex::Group>();
    renderRoot->setName("renderRoot");
    prepareIblRenderGraph->setRenderOperationNode("rendering", renderRoot);

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
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_VERTEX_BIT },
      { 2, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 3, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 4, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    auto descriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(layoutBindings);

    // building pipeline layout
    auto pipelineLayout = std::make_shared<pumex::PipelineLayout>();
    pipelineLayout->descriptorSetLayouts.push_back(descriptorSetLayout);

    auto pipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, pipelineLayout);
    // loading vertex and fragment shader
    pipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/ibl_render.vert.spv"), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/ibl_render.frag.spv"), "main" }
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

    // AssetNode class is a simple class that binds vertex and index buffers and also performs vkCmdDrawIndexed call on a model
    std::shared_ptr<pumex::AssetNode> assetNode = std::make_shared<pumex::AssetNode>(asset, verticesAllocator, 1, 0);
    assetNode->setName("assetNode");
    pipeline->addChild(assetNode);

    // Application data class stores all information required to update rendering ( animation state, camera position, etc )
    std::shared_ptr<ViewerApplicationData> applicationData = std::make_shared<ViewerApplicationData>(buffersAllocator);

    // is this the fastest way to calculate all global transformations for a model ?
    std::vector<glm::mat4> globalTransforms = pumex::calculateResetPosition(*asset);
    PositionData modelData;
    std::copy(begin(globalTransforms), end(globalTransforms), std::begin(modelData.bones));
    (*applicationData->positionData) = modelData;

    // here we create above mentioned uniform buffers - one for camera state and one for model state
    auto cameraUbo      = std::make_shared<pumex::UniformBuffer>(applicationData->cameraBuffer);
    auto positionUbo    = std::make_shared<pumex::UniformBuffer>(applicationData->positionBuffer);

    auto irradianceCubeMapSampler      = std::make_shared<pumex::CombinedImageSampler>("irradiance_map", sampler);
    auto prefEnvironmentCubeMapSampler = std::make_shared<pumex::CombinedImageSampler>("prefiltered_environment_map", sampler);
    auto brdfSampler                   = std::make_shared<pumex::CombinedImageSampler>("brdf_map", sampler);

    auto descriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorPool, descriptorSetLayout);
      descriptorSet->setDescriptor(0, cameraUbo);
      descriptorSet->setDescriptor(1, positionUbo);
      descriptorSet->setDescriptor(2, irradianceCubeMapSampler);
      descriptorSet->setDescriptor(3, prefEnvironmentCubeMapSampler);
      descriptorSet->setDescriptor(4, brdfSampler);
      pipeline->setDescriptorSet(0, descriptorSet);

    // lets add object that calculates time statistics and is able to render it
    std::shared_ptr<pumex::TimeStatisticsHandler> tsHandler = std::make_shared<pumex::TimeStatisticsHandler>(viewer, pipelineCache, buffersAllocator, texturesAllocator, applicationData->textCameraBuffer);
    viewer->addInputEventHandler(tsHandler);
    renderRoot->addChild(tsHandler->getRoot());

    // camera handler processes input events at the beggining of the update phase
    std::shared_ptr<pumex::BasicCameraHandler> bcamHandler = std::make_shared<pumex::BasicCameraHandler>();
    viewer->addInputEventHandler(bcamHandler);
    applicationData->setCameraHandler(bcamHandler);

    // connect render graph to a surface
    std::vector<pumex::QueueTraits> queueTraits{ { VK_QUEUE_GRAPHICS_BIT, 0, 0.75f, pumex::qaExclusive } };
    viewer->compileRenderGraph(prepareIblRenderGraph, queueTraits);
    surface->addRenderGraph(prepareIblRenderGraph->name, true);

    // We must connect update graph that works independently from render graph
    tbb::flow::continue_node< tbb::flow::continue_msg > update(viewer->updateGraph, [=](tbb::flow::continue_msg)
    {
      applicationData->update(viewer);
    });
    tbb::flow::make_edge(viewer->opStartUpdateGraph, update);
    tbb::flow::make_edge(update, viewer->opEndUpdateGraph);

    // events are used to call application data update methods. These methods generate data visisble by renderer through uniform buffers
    viewer->setEventRenderStart( std::bind( &ViewerApplicationData::prepareModelForRendering, applicationData, std::placeholders::_1, asset) );
    surface->setEventSurfaceRenderStart( std::bind(&ViewerApplicationData::prepareCameraForRendering, applicationData, std::placeholders::_1) );
    // object calculating statistics must be also connected as an event
    surface->setEventSurfacePrepareStatistics(std::bind(&pumex::TimeStatisticsHandler::collectData, tsHandler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // main renderer loop is inside Viewer::run()
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
  // here are all windows, surfaces, devices, render graphs and scene graphs destroyed
  viewer->cleanup();
  FLUSH_LOG;
  return 0;
}
