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
#include <pumex/utils/Shapes.h>
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
  #include <android_native_app_glue.h>
  #include <pumex/platform/android/WindowAndroid.h>
#endif  

// This example shows how to setup basic deferred renderer with antialiasing.
// Render graph defines three render operations :
// - first one fills zbuffer
// - second one fills gbuffers with data
// - third one renders lights using gbuffers as input

const uint32_t              MAX_BONES       = 255;
const uint32_t              MODEL_SPONZA_ID = 1;

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
  const uint32_t DEFAULT_SAMPLES_PER_PIXEL = 2;
  const VkPresentModeKHR DEFERRED_DEFAULT_PRESENT_MODE = VK_PRESENT_MODE_FIFO_KHR;
#else
  const uint32_t DEFAULT_SAMPLES_PER_PIXEL = 4;
  const VkPresentModeKHR DEFERRED_DEFAULT_PRESENT_MODE = VK_PRESENT_MODE_MAILBOX_KHR;
#endif



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
  uint32_t  typeID;
  // std430 ?
};

// MaterialData stores information about texture indices. This structure is produced by MaterialSet and therefore it must implement registerProperties() and registerTextures() methods.
// This structure will be used through a storage buffer in shaders
struct MaterialData
{
  uint32_t  diffuseTextureIndex   = 0;
  uint32_t  roughnessTextureIndex = 0;
  uint32_t  metallicTextureIndex  = 0;
  uint32_t  normalTextureIndex    = 0;

  // two functions that define material parameters according to data from an asset's material
  void registerProperties(const pumex::Material& material)
  {
  }
  void registerTextures(const std::map<pumex::TextureSemantic::Type, uint32_t>& textureIndices)
  {
    auto it = textureIndices.find(pumex::TextureSemantic::Diffuse);
    diffuseTextureIndex   = (it == end(textureIndices)) ? 0 : it->second;
    it = textureIndices.find(pumex::TextureSemantic::Specular);
    roughnessTextureIndex = (it == end(textureIndices)) ? 0 : it->second;
    it = textureIndices.find(pumex::TextureSemantic::LightMap);
    metallicTextureIndex  = (it == end(textureIndices)) ? 0 : it->second;
    it = textureIndices.find(pumex::TextureSemantic::Normals);
    normalTextureIndex    = (it == end(textureIndices)) ? 0 : it->second;
  }
};

// simple light point sent to GPU in a storage buffer
struct LightPointData
{
  LightPointData()
  {
  }

  LightPointData(const glm::vec3& pos, const glm::vec3& col, const glm::vec3& att)
    : position{ pos.x, pos.y, pos.z, 0.0f }, color{ col.x, col.y, col.z, 1.0f }, attenuation{ att.x, att.y, att.z, 1.0f }
  {
  }
  glm::vec4 position;
  glm::vec4 color;
  glm::vec4 attenuation;
};

struct DeferredApplicationData
{
  DeferredApplicationData(std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator)
  {
    cameraBuffer     = std::make_shared<pumex::Buffer<pumex::Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce, true);
    textCameraBuffer = std::make_shared<pumex::Buffer<pumex::Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce, true);
    positionData     = std::make_shared<PositionData>();
    positionBuffer   = std::make_shared<pumex::Buffer<PositionData>>(positionData, buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerDevice, pumex::swOnce);

    auto lights = std::make_shared<std::vector<LightPointData>>();
    lights->push_back( LightPointData(glm::vec3(-6.178, -1.434, 1.439), glm::vec3(5.0, 5.0, 5.0), glm::vec3(0.0, 0.0, 1.0)) );
    lights->push_back( LightPointData(glm::vec3(-6.178, 2.202, 1.439),  glm::vec3(5.0, 0.1, 0.1), glm::vec3(0.0, 0.0, 1.0)) );
    lights->push_back( LightPointData(glm::vec3(4.883, 2.202, 1.439),   glm::vec3(0.1, 0.1, 5.0), glm::vec3(0.0, 0.0, 1.0)) );
    lights->push_back( LightPointData(glm::vec3(4.883, -1.434, 1.439),  glm::vec3(0.1, 5.0, 0.1), glm::vec3(0.0, 0.0, 1.0)) );
    lightsBuffer = std::make_shared<pumex::Buffer<std::vector<LightPointData>>>(lights, buffersAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pumex::pbPerDevice, pumex::swOnce);
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
    std::shared_ptr<pumex::Viewer> viewer = surface->viewer.lock();
    float deltaTime       = pumex::inSeconds(viewer->getRenderTimeDelta());
    float renderTime      = pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()) + deltaTime;
    uint32_t renderWidth  = surface->swapChainSize.width;
    uint32_t renderHeight = surface->swapChainSize.height;
    glm::mat4 viewMatrix  = camHandler->getViewMatrix(surface.get());

    pumex::Camera camera;
    camera.setViewMatrix(viewMatrix);
    camera.setObserverPosition(camHandler->getObserverPosition(surface.get()));
    camera.setTimeSinceStart(renderTime);
    camera.setProjectionMatrix(glm::perspective(glm::radians(60.0f), (float)renderWidth / (float)renderHeight, 0.1f, 10000.0f));
    cameraBuffer->setData(surface.get(), camera);

    pumex::Camera textCamera;
    textCamera.setProjectionMatrix(glm::ortho(0.0f, (float)renderWidth, 0.0f, (float)renderHeight), false);
    textCameraBuffer->setData(surface.get(), textCamera);
  }

  void prepareModelForRendering(pumex::Viewer* viewer, std::shared_ptr<pumex::AssetBuffer> assetBuffer, uint32_t modelTypeID)
  {
    std::shared_ptr<pumex::Asset> assetX = assetBuffer->getAsset(modelTypeID, 0);
    if (assetX->animations.empty())
      return;

    float deltaTime          = pumex::inSeconds(viewer->getRenderTimeDelta());
    float renderTime         = pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()) + deltaTime;
    pumex::Animation& anim   = assetX->animations[0];
    pumex::Skeleton& skel    = assetX->skeleton;
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

  void finishFrame(std::shared_ptr<pumex::Viewer> viewer, std::shared_ptr<pumex::Surface> surface)
  {
  }

  std::shared_ptr<pumex::Buffer<pumex::Camera>>               cameraBuffer;
  std::shared_ptr<pumex::Buffer<pumex::Camera>>               textCameraBuffer;
  std::shared_ptr<PositionData>                               positionData;
  std::shared_ptr<pumex::Buffer<PositionData>>                positionBuffer;
  std::shared_ptr<pumex::Buffer<std::vector<LightPointData>>> lightsBuffer;
  std::shared_ptr<pumex::BasicCameraHandler>                  camHandler;
};

int deferred_main( int argc, char * argv[] )
{
  SET_LOG_WARNING;

  std::unordered_map<std::string, uint32_t> availableSamplesPerPixel
  {
    {  "1", 1 },
    {  "2", 2 },
    {  "4", 4 },
    {  "8", 8 }
  };

  args::ArgumentParser                              parser("pumex example : deferred rendering with physically based rendering and antialiasing");
  args::HelpFlag                                    help(parser, "help", "display this help menu", { 'h', "help" });
  args::Flag                                        enableDebugging(parser, "debug", "enable Vulkan debugging", { 'd' });
  args::Flag                                        useFullScreen(parser, "fullscreen", "create fullscreen window", { 'f' });
  args::MapFlag<std::string, VkPresentModeKHR>      presentationMode(parser, "presentation_mode", "presentation mode (immediate, mailbox, fifo, fifo_relaxed)", { 'p' }, pumex::Surface::nameToPresentationModes, DEFERRED_DEFAULT_PRESENT_MODE);
  args::ValueFlag<uint32_t>                         updatesPerSecond(parser, "update_frequency", "number of update calls per second", { 'u' }, 60);
  args::Flag                                        skipDepthPrepass(parser, "nodp", "skip depth prepass", { 'n' });
  args::MapFlag<std::string, uint32_t>              samplesPerPixel(parser, "samples", "samples per pixel (1,2,4,8)", { 's' }, availableSamplesPerPixel, DEFAULT_SAMPLES_PER_PIXEL);
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
  VkPresentModeKHR presentMode = args::get(presentationMode);
  uint32_t updateFrequency     = std::max(1U, args::get(updatesPerSecond));
  uint32_t sampleCount         = args::get(samplesPerPixel);

  LOG_INFO << "Deferred rendering with physically based rendering and antialiasing : ";
  if (enableDebugging)
    LOG_INFO << "Vulkan debugging enabled, ";
  if (!skipDepthPrepass)
    LOG_INFO << "depth prepass present, ";
  else
    LOG_INFO << "depth prepass NOT present, ";
  switch (sampleCount)
  {
  case 1: LOG_INFO << "1 sample per pixel"; break;
  case 2: LOG_INFO << "2 samples per pixel"; break;
  case 4: LOG_INFO << "4 samples per pixel"; break;
  case 8: LOG_INFO << "8 samples per pixel"; break;
  default: LOG_INFO << "unknown number of samples per pixel"; break;
  }
  LOG_INFO << std::endl;

  std::vector<std::string> instanceExtensions;
  std::vector<std::string> requestDebugLayers;
  if (enableDebugging)
    requestDebugLayers.push_back("VK_LAYER_LUNARG_standard_validation");
  pumex::ViewerTraits viewerTraits{ "Deferred PBR", instanceExtensions, requestDebugLayers, updateFrequency };
  viewerTraits.debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT;// | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;

  std::shared_ptr<pumex::Viewer> viewer;
  try
  {
    viewer = std::make_shared<pumex::Viewer>(viewerTraits);
    std::shared_ptr<pumex::DeviceMemoryAllocator> frameBufferAllocator = std::make_shared<pumex::DeviceMemoryAllocator>("frameBuffer", VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 512 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    viewer->setFrameBufferAllocator(frameBufferAllocator);

    std::vector<std::string> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device = viewer->addDevice(0, requestDeviceExtensions);

    pumex::WindowTraits windowTraits{ 0, 100, 100, 1024, 768, useFullScreen ? pumex::WindowTraits::FULLSCREEN : pumex::WindowTraits::WINDOW, "Deferred rendering with PBR and antialiasing", true };
    std::shared_ptr<pumex::Window> window = pumex::Window::createNativeWindow(windowTraits);

    pumex::ResourceDefinition swapchainDefinition = pumex::SWAPCHAIN_DEFINITION(VK_FORMAT_B8G8R8A8_UNORM, 1);
    pumex::SurfaceTraits surfaceTraits{ swapchainDefinition, 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, presentMode, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    std::shared_ptr<pumex::Surface> surface = window->createSurface(device, surfaceTraits);

    pumex::ImageSize fullScreenSizeMultisampled{ pumex::isSurfaceDependent, glm::vec2(1.0f,1.0f), 1, 1, sampleCount };
    pumex::ImageSize fullScreenSize{ pumex::isSurfaceDependent, glm::vec2(1.0f,1.0f) };

#if !defined(VK_USE_PLATFORM_ANDROID_KHR)
    pumex::ResourceDefinition depthSamples(VK_FORMAT_D32_SFLOAT, fullScreenSizeMultisampled, pumex::atDepth);
#else
    pumex::ResourceDefinition depthSamples(VK_FORMAT_D24_UNORM_S8_UINT, fullScreenSizeMultisampled, pumex::atDepth);
#endif
    pumex::ResourceDefinition vec3Samples(VK_FORMAT_R16G16B16A16_SFLOAT, fullScreenSizeMultisampled, pumex::atColor);
    pumex::ResourceDefinition colorSamples(VK_FORMAT_B8G8R8A8_UNORM,     fullScreenSizeMultisampled, pumex::atColor);
    pumex::ResourceDefinition resolveSamples(VK_FORMAT_B8G8R8A8_UNORM,   fullScreenSizeMultisampled, pumex::atColor);
    pumex::ResourceDefinition color(VK_FORMAT_B8G8R8A8_UNORM,            fullScreenSize,             pumex::atColor);

    std::shared_ptr<pumex::RenderGraph> renderGraph = std::make_shared<pumex::RenderGraph>("deferred_render_graph");

    pumex::RenderOperation zPrepass("zPrepass", pumex::opGraphics, fullScreenSizeMultisampled);
      zPrepass.setAttachmentDepthOutput("depth", depthSamples, pumex::loadOpClear(glm::vec2(1.0f, 0.0f)), pumex::ImageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1));

    pumex::RenderOperation gbuffer("gbuffer", pumex::opGraphics, fullScreenSizeMultisampled);
      gbuffer.addAttachmentOutput("position",   vec3Samples,  pumex::loadOpClear(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));
      gbuffer.addAttachmentOutput("normals",    vec3Samples,  pumex::loadOpClear(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)));
      gbuffer.addAttachmentOutput("albedo",     colorSamples, pumex::loadOpClear(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)));
      gbuffer.addAttachmentOutput("pbr",        colorSamples, pumex::loadOpClear(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)));
    if (!skipDepthPrepass)
      gbuffer.setAttachmentDepthInput("depth", depthSamples, pumex::loadOpDontCare());
    else
      gbuffer.setAttachmentDepthOutput("depth", depthSamples, pumex::loadOpClear(glm::vec2(1.0f, 0.0f)));

    pumex::RenderOperation lighting("lighting", pumex::opGraphics, fullScreenSizeMultisampled);
      lighting.addAttachmentInput("position",      vec3Samples,    pumex::loadOpDontCare());
      lighting.addAttachmentInput("normals",       vec3Samples,    pumex::loadOpDontCare());
      lighting.addAttachmentInput("albedo",        colorSamples,   pumex::loadOpDontCare());
      lighting.addAttachmentInput("pbr",           colorSamples,   pumex::loadOpDontCare());
      lighting.setAttachmentDepthInput("depth",    depthSamples,   pumex::loadOpDontCare());
      lighting.addAttachmentOutput("resolve",      resolveSamples, pumex::loadOpDontCare());
      lighting.addAttachmentResolveOutput(pumex::SWAPCHAIN_NAME, swapchainDefinition, pumex::loadOpDontCare(), pumex::ImageSubresourceRange(), 0, true, "resolve" );

    if (!skipDepthPrepass)
      renderGraph->addRenderOperation(zPrepass);
    renderGraph->addRenderOperation(gbuffer);
    renderGraph->addRenderOperation(lighting);

    if (!skipDepthPrepass)
      renderGraph->addResourceTransition("zPrepass",  "depth", "gbuffer",  "depth" );
    renderGraph->addResourceTransition("gbuffer", "position", "lighting", "position");
    renderGraph->addResourceTransition("gbuffer",  "normals",  "lighting",  "normals");
    renderGraph->addResourceTransition("gbuffer",  "albedo",   "lighting",  "albedo");
    renderGraph->addResourceTransition("gbuffer",  "pbr",      "lighting",  "pbr");
    if(!skipDepthPrepass)
      renderGraph->addResourceTransition("zPrepass",  "depth",    "lighting",  "depth");
    else
      renderGraph->addResourceTransition("gbuffer", "depth",    "lighting", "depth");

    std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator = std::make_shared<pumex::DeviceMemoryAllocator>("buffers", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 64 MB for vertex and index buffers
    std::shared_ptr<pumex::DeviceMemoryAllocator> verticesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>("vertices", VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 64 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 80 MB memory for textures
    std::shared_ptr<pumex::DeviceMemoryAllocator> texturesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>("textures", VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 80 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // create common descriptor pool
    std::shared_ptr<pumex::DescriptorPool> descriptorPool = std::make_shared<pumex::DescriptorPool>();

    std::shared_ptr<DeferredApplicationData> applicationData = std::make_shared<DeferredApplicationData>(buffersAllocator);

    auto pipelineCache = std::make_shared<pumex::PipelineCache>();

    std::vector<pumex::VertexSemantic> requiredSemantic = { { pumex::VertexSemantic::Position, 3 },{ pumex::VertexSemantic::Normal, 3 },{ pumex::VertexSemantic::Tangent, 3 },{ pumex::VertexSemantic::TexCoord, 3 },{ pumex::VertexSemantic::BoneIndex, 1 },{ pumex::VertexSemantic::BoneWeight, 1 } };

    std::vector<pumex::AssetBufferVertexSemantics> assetSemantics = { { 1, requiredSemantic } };
    std::shared_ptr<pumex::AssetBuffer> assetBuffer = std::make_shared<pumex::AssetBuffer>(assetSemantics, buffersAllocator, verticesAllocator);

    std::vector<pumex::TextureSemantic> textureSemantic = { { pumex::TextureSemantic::Diffuse, 0 },{ pumex::TextureSemantic::Specular, 1 },{ pumex::TextureSemantic::LightMap, 2 },{ pumex::TextureSemantic::Normals, 3 } };
    std::shared_ptr<pumex::TextureRegistryArrayOfTextures> textureRegistry = std::make_shared<pumex::TextureRegistryArrayOfTextures>(buffersAllocator, texturesAllocator);
    textureRegistry->setSampledImage(0);
    textureRegistry->setSampledImage(1);
    textureRegistry->setSampledImage(2);
    textureRegistry->setSampledImage(3);
    std::shared_ptr<pumex::MaterialRegistry<MaterialData>> materialRegistry = std::make_shared<pumex::MaterialRegistry<MaterialData>>(buffersAllocator);
    std::shared_ptr<pumex::MaterialSet> materialSet = std::make_shared<pumex::MaterialSet>(viewer, materialRegistry, textureRegistry, buffersAllocator, textureSemantic);

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    viewer->setAssetTextureRename("\\.dds", "_mobi.ktx");
#endif
    std::shared_ptr<pumex::Asset> asset = viewer->loadAsset("sponza/sponza.dae", false, requiredSemantic);
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    viewer->clearAssetTextureRename();
#endif

    pumex::BoundingBox bbox = pumex::calculateBoundingBox(*asset, 1);

    assetBuffer->registerType(MODEL_SPONZA_ID, pumex::AssetTypeDefinition(bbox));
    assetBuffer->registerObjectLOD(MODEL_SPONZA_ID, pumex::AssetLodDefinition(0.0f, 10000.0f), asset);
    materialSet->registerMaterials(MODEL_SPONZA_ID, asset);
    materialSet->endRegisterMaterials();

    auto assetBufferNode = std::make_shared<pumex::AssetBufferNode>(assetBuffer, materialSet, 1, 0);
    assetBufferNode->setName("assetBufferNode");

    std::shared_ptr<pumex::AssetBufferDrawObject> modelDraw = std::make_shared<pumex::AssetBufferDrawObject>(MODEL_SPONZA_ID);
    modelDraw->setName("modelDraw");
    assetBufferNode->addChild(modelDraw);

    std::vector<glm::mat4> globalTransforms = pumex::calculateResetPosition(*asset);
    PositionData modelData;
    std::copy(begin(globalTransforms), end(globalTransforms), std::begin(modelData.bones));
    modelData.typeID                  = MODEL_SPONZA_ID;
    (*applicationData->positionData)  = modelData;

    auto cameraUbo  = std::make_shared<pumex::UniformBuffer>(applicationData->cameraBuffer);
    auto sampler    = std::make_shared<pumex::Sampler>(pumex::SamplerTraits());
    /***********************************/

    if (!skipDepthPrepass)
    {
      auto buildzRoot = std::make_shared<pumex::Group>();
      buildzRoot->setName("buildzRoot");
      renderGraph->setRenderOperationNode("zPrepass", buildzRoot);

      std::vector<pumex::DescriptorSetLayoutBinding> buildzLayoutBindings =
      {
        { 0, 1,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 1, 1,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 2, 1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 3, 1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 4, 1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
        { 5, 64, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT },
        { 6, 1,  VK_DESCRIPTOR_TYPE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
      };
      auto buildzDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(buildzLayoutBindings);

      // building gbufferPipeline layout
      auto buildzPipelineLayout = std::make_shared<pumex::PipelineLayout>();
      buildzPipelineLayout->descriptorSetLayouts.push_back(buildzDescriptorSetLayout);

      auto buildzPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, buildzPipelineLayout);
      buildzPipeline->setName("buildzPipeline");

      buildzPipeline->shaderStages =
      {
        { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/deferred_buildz.vert.spv"), "main" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/deferred_buildz.frag.spv"), "main" }
      };
      buildzPipeline->vertexInput =
      {
        { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
      };
      buildzPipeline->rasterizationSamples = pumex::makeSamples(sampleCount);

      buildzRoot->addChild(buildzPipeline);

      // node will be added twice - first one - for building depth buffer, and second one for filling gbuffers
      buildzPipeline->addChild(assetBufferNode);

      std::shared_ptr<pumex::DescriptorSet> bzDescriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorPool, buildzDescriptorSetLayout);
      bzDescriptorSet->setDescriptor(0, cameraUbo);
      bzDescriptorSet->setDescriptor(1, std::make_shared<pumex::UniformBuffer>(applicationData->positionBuffer));
      bzDescriptorSet->setDescriptor(2, std::make_shared<pumex::StorageBuffer>(materialSet->typeDefinitionBuffer));
      bzDescriptorSet->setDescriptor(3, std::make_shared<pumex::StorageBuffer>(materialSet->materialVariantBuffer));
      bzDescriptorSet->setDescriptor(4, std::make_shared<pumex::StorageBuffer>(materialRegistry->materialDefinitionBuffer));
      bzDescriptorSet->setDescriptor(5, textureRegistry->getResources(0));
      bzDescriptorSet->setDescriptor(6, sampler);
      buildzPipeline->setDescriptorSet(0, bzDescriptorSet);
    }

    /***********************************/

    auto gbufferRoot = std::make_shared<pumex::Group>();
    gbufferRoot->setName("gbufferRoot");
    renderGraph->setRenderOperationNode("gbuffer", gbufferRoot);

    std::vector<pumex::DescriptorSetLayoutBinding> gbufferLayoutBindings =
    {
      { 0, 1,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 2, 1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 3, 1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 4, 1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 5, 64, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 6, 64, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 7, 64, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 8, 64, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 9, 1,  VK_DESCRIPTOR_TYPE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }

    };
    auto gbufferDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(gbufferLayoutBindings);

    // building gbufferPipeline layout
    auto gbufferPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    gbufferPipelineLayout->descriptorSetLayouts.push_back(gbufferDescriptorSetLayout);

    auto gbufferPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, gbufferPipelineLayout);
    gbufferPipeline->setName("gbufferPipeline");

    if (!skipDepthPrepass)
    {
      gbufferPipeline->depthWriteEnable = VK_FALSE;
      gbufferPipeline->depthCompareOp = VK_COMPARE_OP_EQUAL;
    }

    gbufferPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/deferred_gbuffers.vert.spv"), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/deferred_gbuffers.frag.spv"), "main" }
    };
    gbufferPipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
    };
    gbufferPipeline->blendAttachments =
    {
      { VK_FALSE, 0xF },
      { VK_FALSE, 0xF },
      { VK_FALSE, 0xF },
      { VK_FALSE, 0xF }
    };
    gbufferPipeline->rasterizationSamples = pumex::makeSamples(sampleCount);

    gbufferRoot->addChild(gbufferPipeline);

    gbufferPipeline->addChild(assetBufferNode);

    std::shared_ptr<pumex::DescriptorSet> descriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorPool, gbufferDescriptorSetLayout);
    descriptorSet->setDescriptor(0, cameraUbo);
    descriptorSet->setDescriptor(1, std::make_shared<pumex::UniformBuffer>(applicationData->positionBuffer));
    descriptorSet->setDescriptor(2, std::make_shared<pumex::StorageBuffer>(materialSet->typeDefinitionBuffer));
    descriptorSet->setDescriptor(3, std::make_shared<pumex::StorageBuffer>(materialSet->materialVariantBuffer));
    descriptorSet->setDescriptor(4, std::make_shared<pumex::StorageBuffer>(materialRegistry->materialDefinitionBuffer));
    descriptorSet->setDescriptor(5, textureRegistry->getResources(0));
    descriptorSet->setDescriptor(6, textureRegistry->getResources(1));
    descriptorSet->setDescriptor(7, textureRegistry->getResources(2));
    descriptorSet->setDescriptor(8, textureRegistry->getResources(3));
    descriptorSet->setDescriptor(9, sampler);
    gbufferPipeline->setDescriptorSet(0, descriptorSet);

/**********************/

    auto lightingRoot = std::make_shared<pumex::Group>();
    lightingRoot->setName("lightingRoot");
    renderGraph->setRenderOperationNode("lighting", lightingRoot);

    std::shared_ptr<pumex::Asset> fullScreenTriangle = pumex::createFullScreenTriangle();

    std::vector<pumex::DescriptorSetLayoutBinding> compositeLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,   VK_SHADER_STAGE_FRAGMENT_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,   VK_SHADER_STAGE_FRAGMENT_BIT },
      { 2, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 3, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 4, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 5, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    auto compositeDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(compositeLayoutBindings);

    // building gbufferPipeline layout
    auto compositePipelineLayout = std::make_shared<pumex::PipelineLayout>();
    compositePipelineLayout->descriptorSetLayouts.push_back(compositeDescriptorSetLayout);

    auto compositePipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, compositePipelineLayout);
    compositePipeline->setName("compositePipeline");
    compositePipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/deferred_composite.vert.spv"), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer, "shaders/deferred_composite.frag.spv"), "main" }
    };
    compositePipeline->depthTestEnable = VK_FALSE;
    compositePipeline->depthWriteEnable = VK_FALSE;

    compositePipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, fullScreenTriangle->geometries[0].semantic }
    };
    compositePipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    compositePipeline->rasterizationSamples = pumex::makeSamples(sampleCount);

    lightingRoot->addChild(compositePipeline);

    std::shared_ptr<pumex::AssetNode> assetNode = std::make_shared<pumex::AssetNode>(fullScreenTriangle, verticesAllocator, 1, 0);
    assetNode->setName("fullScreenTriangleAssetNode");
    compositePipeline->addChild(assetNode);

    auto iaSampler = std::make_shared<pumex::Sampler>(pumex::SamplerTraits());

    auto compositeDescriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorPool, compositeDescriptorSetLayout);
    compositeDescriptorSet->setDescriptor(0, cameraUbo);
    compositeDescriptorSet->setDescriptor(1, std::make_shared<pumex::StorageBuffer>(applicationData->lightsBuffer));
    compositeDescriptorSet->setDescriptor(2, std::make_shared<pumex::InputAttachment>("position", iaSampler));
    compositeDescriptorSet->setDescriptor(3, std::make_shared<pumex::InputAttachment>("normals", iaSampler));
    compositeDescriptorSet->setDescriptor(4, std::make_shared<pumex::InputAttachment>("albedo", iaSampler));
    compositeDescriptorSet->setDescriptor(5, std::make_shared<pumex::InputAttachment>("pbr", iaSampler));
    assetNode->setDescriptorSet(0, compositeDescriptorSet);

    std::shared_ptr<pumex::TimeStatisticsHandler> tsHandler = std::make_shared<pumex::TimeStatisticsHandler>(viewer, pipelineCache, buffersAllocator, texturesAllocator, applicationData->textCameraBuffer, pumex::makeSamples(sampleCount));
    viewer->addInputEventHandler(tsHandler);
    lightingRoot->addChild(tsHandler->getRoot());

    std::shared_ptr<pumex::BasicCameraHandler> bcamHandler = std::make_shared<pumex::BasicCameraHandler>();
    bcamHandler->setCameraVelocity(4.0f, 12.0f);
    viewer->addInputEventHandler(bcamHandler);
    applicationData->setCameraHandler(bcamHandler);

    // connect render graph to a surface
    std::vector<pumex::QueueTraits> queueTraits{ { VK_QUEUE_GRAPHICS_BIT, 0, 0.75f, pumex::qaExclusive } };
    viewer->compileRenderGraph(renderGraph, queueTraits);
    surface->addRenderGraph(renderGraph->name, true);

    // build simple update graph
    tbb::flow::continue_node< tbb::flow::continue_msg > update(viewer->updateGraph, [=](tbb::flow::continue_msg)
    {
      applicationData->update(viewer);
    });
    tbb::flow::make_edge(viewer->opStartUpdateGraph, update);
    tbb::flow::make_edge(update, viewer->opEndUpdateGraph);

    // set render callbacks to application data
    viewer->setEventRenderStart(std::bind(&DeferredApplicationData::prepareModelForRendering, applicationData, std::placeholders::_1, assetBuffer, MODEL_SPONZA_ID));
    surface->setEventSurfaceRenderStart(std::bind(&DeferredApplicationData::prepareCameraForRendering, applicationData, std::placeholders::_1));
    surface->setEventSurfacePrepareStatistics(std::bind(&pumex::TimeStatisticsHandler::collectData, tsHandler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

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

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
void android_main(struct android_app *app)
{
  pumex::WindowAndroid::runMain(app, deferred_main);
}
#else
int main(int argc, char* argv[])
{
  return deferred_main(argc, argv);
}
#endif	
