//
// Copyright(c) 2017 Pawe³ Ksiê¿opolski ( pumexx )
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

#include <random>
#include <sstream>
#include <iomanip>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/simd/matrix.h>
#include <gli/gli.hpp>
#include <tbb/tbb.h>
#include <pumex/Pumex.h>
#include <pumex/AssetLoaderAssimp.h>
// suppression of noexcept keyword used in args library ( so that code may compile on VS 2013 )
#ifdef _MSC_VER
  #if _MSC_VER<1900
    #define noexcept 
  #endif
#endif
#include <args.hxx>


// This example shows how to render multiple different objects using a minimal number of vkCmdDrawIndexedIndirect commands
// ( the number of draw calls is equal to number of rendered object types ).
// Each object type may be drawn with different sets of textures, because all textures used in rendering are stored in texture array
// ( different set of textures for the same object is called "material variant" in that example ).
//
// This example also shows, how to animate assets and how to render different assets ( people, clothes ) using the same animated skeleton.
//
// Rendering consists of following parts :
// 1. Positions and parameters of all objects are sent to compute shader. Compute shader ( a filter ) culls invisible objects using 
//    camera parameters, object position and object bounding box. For visible objects the appropriate level of detail is chosen. 
//    Results are stored in a buffer.
// 2. Above mentioned buffer is used during rendering to choose appropriate object parameters ( position, bone matrices, object specific parameters, material ids, etc )


const uint32_t MAX_SURFACES = 6;
const uint32_t MAX_BONES = 63;
const uint32_t MAIN_RENDER_MASK = 1;


// Structure storing information about people and objects.
// Structure is used by update loop to update its parameters.
// Then it is sent to a render loop and used to produce a render data ( PositionData and InstanceData )

struct ObjectData
{
  ObjectData()
    : animation{ 0 }, animationOffset{ 0.0f }, typeID{ 0 }, materialVariant{ 0 }, time2NextTurn { 0.0f }, ownerID{ UINT32_MAX }
  {
  }
  pumex::Kinematic kinematic;       // not used by clothes
  uint32_t         animation;       // not used by clothes
  float            animationOffset; // not used by clothes
  uint32_t         typeID;
  uint32_t         materialVariant;
  float            time2NextTurn;   // not used by clothes
  uint32_t         ownerID;         // not used by people
};

struct UpdateData
{
  UpdateData()
  {
  }
  glm::vec3                                cameraPosition;
  glm::vec2                                cameraGeographicCoordinates;
  float                                    cameraDistance;

  std::unordered_map<uint32_t, ObjectData> people;
  std::unordered_map<uint32_t, ObjectData> clothes;

  glm::vec2                                lastMousePos;
  bool                                     leftMouseKeyPressed;
  bool                                     rightMouseKeyPressed;
  
  bool                                     moveForward;
  bool                                     moveBackward;
  bool                                     moveLeft;
  bool                                     moveRight;
  bool                                     moveUp;
  bool                                     moveDown;
  bool                                     moveFast;
  bool                                     measureTime;
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

  std::vector<ObjectData> people;
  std::vector<ObjectData> clothes;
  std::vector<uint32_t>   clothOwners;
};

struct PositionData
{
  PositionData(const glm::mat4& p = glm::mat4())
    : position{p}
  {
  }
  glm::mat4 position;
  glm::mat4 bones[MAX_BONES];
};

struct InstanceData
{
  InstanceData(uint32_t p=0, uint32_t t=0, uint32_t m=0, uint32_t i=0)
    : positionIndex{ p }, typeID{ t }, materialVariant{ m }, mainInstance {i}
  {
  }
  uint32_t positionIndex;
  uint32_t typeID;
  uint32_t materialVariant;
  uint32_t mainInstance;
};

struct MaterialData
{
  glm::vec4 ambient;
  glm::vec4 diffuse;
  glm::vec4 specular;
  float     shininess;
  uint32_t  diffuseTextureIndex = 0;
  uint32_t  std430pad0;
  uint32_t  std430pad1;

  // two functions that define material parameters according to data from an asset's material 
  void registerProperties(const pumex::Material& material)
  {
    ambient   = material.getProperty("$clr.ambient", glm::vec4(0, 0, 0, 0));
    diffuse   = material.getProperty("$clr.diffuse", glm::vec4(1, 1, 1, 1));
    specular  = material.getProperty("$clr.specular", glm::vec4(0, 0, 0, 0));
    shininess = material.getProperty("$mat.shininess", glm::vec4(0, 0, 0, 0)).r;
  }
  void registerTextures(const std::map<pumex::TextureSemantic::Type, uint32_t>& textureIndices)
  {
    auto it = textureIndices.find(pumex::TextureSemantic::Diffuse);
    diffuseTextureIndex = (it == textureIndices.end()) ? 0 : it->second;
  }
};

struct SkelAnimKey
{
  SkelAnimKey(uint32_t s, uint32_t a)
    : skelID{ s }, animID{ a }
  {
  }
  uint32_t skelID;
  uint32_t animID;
};

inline bool operator<(const SkelAnimKey& lhs, const SkelAnimKey& rhs)
{
  if (lhs.skelID != rhs.skelID)
    return lhs.animID < rhs.animID;
  return lhs.skelID < rhs.skelID;
}

struct CrowdApplicationData
{
  std::weak_ptr<pumex::Viewer>                           viewer;

  UpdateData                                             updateData;
  std::array<RenderData, 3>                              renderData;

  glm::vec3                                              minArea;
  glm::vec3                                              maxArea;
  std::vector<pumex::Skeleton>                           skeletons;
  std::vector<pumex::Animation>                          animations;
  std::map<SkelAnimKey, std::vector<uint32_t>>           skelAnimBoneMapping;
  std::vector<float>                                     animationSpeed;

  std::default_random_engine                             randomEngine;
  std::exponential_distribution<float>                   randomTime2NextTurn;
  std::uniform_real_distribution<float>                  randomRotation;
  std::uniform_int_distribution<uint32_t>                randomAnimation;

  std::shared_ptr<pumex::DeviceMemoryAllocator>          buffersAllocator;
  std::shared_ptr<pumex::DeviceMemoryAllocator>          verticesAllocator;
  std::shared_ptr<pumex::DeviceMemoryAllocator>          texturesAllocator;
  std::shared_ptr<pumex::AssetBuffer>                    skeletalAssetBuffer;
  std::shared_ptr<pumex::AssetBufferInstancedResults>    instancedResults;
  std::shared_ptr<pumex::TextureRegistryTextureArray>    textureRegistry;
  std::shared_ptr<pumex::MaterialSet<MaterialData>>      materialSet;

  std::shared_ptr<pumex::UniformBufferPerSurface<pumex::Camera>> cameraUbo;
  std::shared_ptr<pumex::StorageBuffer<PositionData>>            positionSbo;
  std::shared_ptr<pumex::StorageBuffer<InstanceData>>            instanceSbo;

  std::shared_ptr<pumex::RenderPass>                     defaultRenderPass;

  std::shared_ptr<pumex::PipelineCache>                  pipelineCache;

  std::shared_ptr<pumex::DescriptorSetLayout>            instancedRenderDescriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>                 instancedRenderPipelineLayout;
  std::shared_ptr<pumex::GraphicsPipeline>               instancedRenderPipeline;
  std::shared_ptr<pumex::DescriptorPool>                 instancedRenderDescriptorPool;
  std::shared_ptr<pumex::DescriptorSet>                  instancedRenderDescriptorSet;

  std::shared_ptr<pumex::DescriptorSetLayout>            filterDescriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>                 filterPipelineLayout;
  std::shared_ptr<pumex::ComputePipeline>                filterPipeline;
  std::shared_ptr<pumex::DescriptorPool>                 filterDescriptorPool;
  std::shared_ptr<pumex::DescriptorSet>                  filterDescriptorSet;

  std::shared_ptr<pumex::UniformBufferPerSurface<pumex::Camera>> textCameraUbo;
  std::shared_ptr<pumex::Font>                           fontDefault;
  std::shared_ptr<pumex::Font>                           fontSmall;
  std::shared_ptr<pumex::Text>                           textDefault;
  std::shared_ptr<pumex::Text>                           textSmall;

  std::shared_ptr<pumex::DescriptorSetLayout>            textDescriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>                 textPipelineLayout;
  std::shared_ptr<pumex::GraphicsPipeline>               textPipeline;
  std::shared_ptr<pumex::DescriptorPool>                 textDescriptorPool;
  std::shared_ptr<pumex::DescriptorSet>                  textDescriptorSet;
  std::shared_ptr<pumex::DescriptorSet>                  textDescriptorSetSmall;

  std::shared_ptr<pumex::QueryPool>                      timeStampQueryPool;

  pumex::HPClock::time_point                             lastFrameStart;
  bool                                                   measureTime = true;
  std::mutex                                             measureMutex;
  std::unordered_map<uint32_t, double>                   times;

  std::unordered_map<uint32_t, glm::mat4>                slaveViewMatrix;

  std::unordered_map<pumex::Surface*,std::shared_ptr<pumex::CommandBuffer>> myCmdBuffer;


  CrowdApplicationData(std::shared_ptr<pumex::Viewer> v)
	  : viewer{ v }, randomTime2NextTurn{ 0.25 }, randomRotation{ -glm::pi<float>(), glm::pi<float>() }
  {
  }

  void setup(const glm::vec3& minAreaParam, const glm::vec3& maxAreaParam, float objectDensity)
  {
    minArea = minAreaParam;
    maxArea = maxAreaParam;
    std::shared_ptr<pumex::Viewer> viewerSh = viewer.lock();
    CHECK_LOG_THROW (viewerSh.get() == nullptr, "Cannot acces pumex viewer");

    pumex::AssetLoaderAssimp loader;

    std::vector<std::string> animationFileNames
    {
      "people/wmale1_bbox.dae",
      "people/wmale1_walk.dae",
      "people/wmale1_walk_easy.dae",
      "people/wmale1_walk_big_steps.dae",
      "people/wmale1_run.dae"
    };
    animationSpeed =  // in meters per sec
    {
      0.0f,
      1.0f,
      0.8f,
      1.2f,
      2.0f
    };

    // We assume that animations use the same skeleton as skeletal models
    for (uint32_t i = 0; i < animationFileNames.size(); ++i)
    {
      std::string fullAssetFileName = viewerSh->getFullFilePath(animationFileNames[i]);
      if (fullAssetFileName.empty())
      {
        LOG_WARNING << "Cannot find asset : " << animationFileNames[i] << std::endl;
        continue;
      }
      std::shared_ptr<pumex::Asset> asset(loader.load(fullAssetFileName,true));
      if (asset.get() == nullptr)
      {
        LOG_WARNING << "Cannot load asset : " << fullAssetFileName << std::endl;
        continue;
      }
      animations.push_back(asset->animations[0]);
    }

	randomAnimation = std::uniform_int_distribution<uint32_t>(1, animations.size() - 1);

    // alocate 12 MB for uniform and storage buffers
    buffersAllocator  = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 12 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 64 MB for vertex and index buffers
    verticesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 64 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 80 MB memory for 24 compressed textures and for font textures
    texturesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 80 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);

    std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 },{ pumex::VertexSemantic::Normal, 3 },{ pumex::VertexSemantic::TexCoord, 3 },{ pumex::VertexSemantic::BoneWeight, 4 },{ pumex::VertexSemantic::BoneIndex, 4 } };
    std::vector<pumex::AssetBufferVertexSemantics> assetSemantics = { { MAIN_RENDER_MASK, vertexSemantic } };
    skeletalAssetBuffer = std::make_shared<pumex::AssetBuffer>(assetSemantics, buffersAllocator, verticesAllocator);
    instancedResults    = std::make_shared<pumex::AssetBufferInstancedResults>(assetSemantics, skeletalAssetBuffer, buffersAllocator);

    textureRegistry = std::make_shared<pumex::TextureRegistryTextureArray>();
    textureRegistry->setTargetTexture(0, std::make_shared<pumex::Texture>(gli::texture(gli::target::TARGET_2D_ARRAY, gli::format::FORMAT_RGBA_DXT1_UNORM_BLOCK8, gli::texture::extent_type(2048, 2048, 1), 24, 1, 12), pumex::SamplerTraits(), texturesAllocator));
    std::vector<pumex::TextureSemantic> textureSemantic = { { pumex::TextureSemantic::Diffuse, 0 } };
    materialSet = std::make_shared<pumex::MaterialSet<MaterialData>>(viewerSh, textureRegistry, buffersAllocator, textureSemantic);

    std::vector<std::pair<std::string,bool>> skeletalNames
    {
      { "wmale1", true},
      { "wmale2", true},
      { "wmale3", true},
      { "wmale1_cloth1", false},
      { "wmale1_cloth2", false },
      { "wmale1_cloth3", false },
      { "wmale2_cloth1", false },
      { "wmale2_cloth2", false },
      { "wmale2_cloth3", false },
      { "wmale3_cloth1", false },
      { "wmale3_cloth2", false },
      { "wmale3_cloth3", false }
    };
    std::vector<std::string> skeletalModels
    {
      "people/wmale1_lod0.dae", "people/wmale1_lod1.dae", "people/wmale1_lod2.dae",
      "people/wmale2_lod0.dae", "people/wmale2_lod1.dae", "people/wmale2_lod2.dae",
      "people/wmale3_lod0.dae", "people/wmale3_lod1.dae", "people/wmale3_lod2.dae",
      "people/wmale1_cloth1.dae", "", "", // well, I don't have LODded cloths :(
      "people/wmale1_cloth2.dae", "", "",
      "people/wmale1_cloth3.dae", "", "",
      "people/wmale2_cloth1.dae", "", "",
      "people/wmale2_cloth2.dae", "", "",
      "people/wmale2_cloth3.dae", "", "",
      "people/wmale3_cloth1.dae", "", "",
      "people/wmale3_cloth2.dae", "", "",
      "people/wmale3_cloth3.dae", "", ""
    };
    std::vector<pumex::AssetLodDefinition> lodRanges
    {
      pumex::AssetLodDefinition(0.0f, 8.0f), pumex::AssetLodDefinition(8.0f, 16.0f), pumex::AssetLodDefinition(16.0f, 100.0f),
      pumex::AssetLodDefinition(0.0f, 8.0f), pumex::AssetLodDefinition(8.0f, 16.0f), pumex::AssetLodDefinition(16.0f, 100.0f),
      pumex::AssetLodDefinition(0.0f, 8.0f), pumex::AssetLodDefinition(8.0f, 16.0f), pumex::AssetLodDefinition(16.0f, 100.0f),
      pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f), pumex::AssetLodDefinition(0.0f, 0.0f),
      pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f), pumex::AssetLodDefinition(0.0f, 0.0f),
      pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f), pumex::AssetLodDefinition(0.0f, 0.0f),
      pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f), pumex::AssetLodDefinition(0.0f, 0.0f),
      pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f), pumex::AssetLodDefinition(0.0f, 0.0f),
      pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f), pumex::AssetLodDefinition(0.0f, 0.0f),
      pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f), pumex::AssetLodDefinition(0.0f, 0.0f),
      pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f), pumex::AssetLodDefinition(0.0f, 0.0f),
      pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f), pumex::AssetLodDefinition(0.0f, 0.0f)
   };
    std::multimap<std::string,std::vector<std::vector<std::string>>> materialVariants = 
    {
      { "wmale1", { { "body_mat", "people/young_lightskinned_male_diffuse_1.dds" } } },
      { "wmale1", { { "body_mat", "people/young_lightskinned_male_diffuse.dds" } } },
      { "wmale2", { { "body_mat", "people/young_lightskinned_male_diffuse3_1.dds" } } },
      { "wmale2", { { "body_mat", "people/dragon_female_white.dds" } } },
      { "wmale3", { { "body_mat", "people/middleage_lightskinned_male_diffuse_1.dds"} } },
      { "wmale3", { { "body_mat", "people/ork_texture.dds" } } }
    };
    std::multimap<std::string, std::vector<std::string>> clothVariants =
    {
      { "wmale1", { } },
      { "wmale1", { "wmale1_cloth1" } },
      { "wmale1", { "wmale1_cloth2" } },
      { "wmale1", { "wmale1_cloth3" } },
      { "wmale2", { } },
      { "wmale2", { "wmale2_cloth1" } },
      { "wmale2", { "wmale2_cloth2" } },
      { "wmale2", { "wmale2_cloth3" } },
      { "wmale3", { } },
      { "wmale3", { "wmale3_cloth1" } },
      { "wmale3", { "wmale3_cloth2" } },
      { "wmale3", { "wmale3_cloth3" } }
    };

    std::vector<uint32_t> mainObjectTypeID;
    std::vector<uint32_t> accessoryObjectTypeID;
    skeletons.push_back(pumex::Skeleton()); // empty skeleton for null type
    for (uint32_t i = 0; i < skeletalNames.size(); ++i)
    {
      uint32_t typeID = 0;
      for (uint32_t j = 0; j<3; ++j)
      {
        if (skeletalModels[3 * i + j].empty())
          continue;
        std::string fullAssetFileName = viewerSh->getFullFilePath(skeletalModels[3 * i + j]);
        if (fullAssetFileName.empty())
        {
          LOG_WARNING << "Cannot find asset : " << skeletalModels[3 * i + j] << std::endl;
          continue;
        }
        std::shared_ptr<pumex::Asset> asset(loader.load(fullAssetFileName,false,vertexSemantic));
        if (asset.get() == nullptr)
        {
          LOG_WARNING << "Cannot load asset : " << fullAssetFileName << std::endl;
          continue;
        }
        if( typeID == 0 )
        {
          skeletons.push_back(asset->skeleton);
          pumex::BoundingBox bbox = pumex::calculateBoundingBox(asset->skeleton, animations[0], true);
          typeID = skeletalAssetBuffer->registerType(skeletalNames[i].first, pumex::AssetTypeDefinition(bbox));
          if(skeletalNames[i].second)
            mainObjectTypeID.push_back(typeID);
          else
            accessoryObjectTypeID.push_back(typeID);
        }
        materialSet->registerMaterials(typeID, asset);
        skeletalAssetBuffer->registerObjectLOD(typeID, asset, lodRanges[3 * i + j]);
      }
      // register texture variants
      for (auto it = materialVariants.begin(), eit = materialVariants.end(); it != eit; ++it)
      {
        if (it->first == skeletalNames[i].first)
        {
          uint32_t variantCount = materialSet->getMaterialVariantCount(typeID);
          std::vector<pumex::Material> materials = materialSet->getMaterials(typeID);
          for (auto iit = it->second.begin(); iit != it->second.end(); ++iit)
          {
            for ( auto& mat : materials )
            {
              if (mat.name == (*iit)[0])
                mat.textures[pumex::TextureSemantic::Diffuse] = (*iit)[1];
            }
          }
          materialSet->setMaterialVariant(typeID, variantCount, materials);
        }
      }
    }
    instancedResults->setup();

    materialSet->refreshMaterialStructures();
    std::vector<uint32_t> materialVariantCount(skeletalNames.size()+1);
    for (uint32_t i= 0; i<materialVariantCount.size(); ++i)
      materialVariantCount[i] = materialSet->getMaterialVariantCount(i);

    cameraUbo    = std::make_shared<pumex::UniformBufferPerSurface<pumex::Camera>>(buffersAllocator);
    positionSbo  = std::make_shared<pumex::StorageBuffer<PositionData>>(buffersAllocator, 3);
    instanceSbo  = std::make_shared<pumex::StorageBuffer<InstanceData>>(buffersAllocator, 3);

    pipelineCache = std::make_shared<pumex::PipelineCache>();

    std::vector<pumex::DescriptorSetLayoutBinding> instancedRenderLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
      { 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 7, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    instancedRenderDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(instancedRenderLayoutBindings);
    instancedRenderDescriptorPool = std::make_shared<pumex::DescriptorPool>(3 * MAX_SURFACES, instancedRenderLayoutBindings);
    // building pipeline layout
    instancedRenderPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    instancedRenderPipelineLayout->descriptorSetLayouts.push_back(instancedRenderDescriptorSetLayout);
    instancedRenderPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, instancedRenderPipelineLayout, defaultRenderPass, 0);
    instancedRenderPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/crowd_instanced_animation.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/crowd_instanced_animation.frag.spv")), "main" }
    };
    instancedRenderPipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, vertexSemantic }
    };
    instancedRenderPipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    instancedRenderPipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    instancedRenderDescriptorSet = std::make_shared<pumex::DescriptorSet>(instancedRenderDescriptorSetLayout, instancedRenderDescriptorPool, 3);
    instancedRenderDescriptorSet->setDescriptor(0, cameraUbo);
    instancedRenderDescriptorSet->setDescriptor(1, positionSbo);
    instancedRenderDescriptorSet->setDescriptor(2, instanceSbo);
    instancedRenderDescriptorSet->setDescriptor(3, instancedResults->getOffsetValues(MAIN_RENDER_MASK));
    instancedRenderDescriptorSet->setDescriptor(4, materialSet->typeDefinitionSbo);
    instancedRenderDescriptorSet->setDescriptor(5, materialSet->materialVariantSbo);
    instancedRenderDescriptorSet->setDescriptor(6, materialSet->materialDefinitionSbo);
    instancedRenderDescriptorSet->setDescriptor(7, textureRegistry->getTargetTexture(0));

    std::vector<pumex::DescriptorSetLayoutBinding> filterLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT }
    };
    filterDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(filterLayoutBindings);
    filterDescriptorPool = std::make_shared<pumex::DescriptorPool>(3 * MAX_SURFACES, filterLayoutBindings);
    // building pipeline layout
    filterPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    filterPipelineLayout->descriptorSetLayouts.push_back(filterDescriptorSetLayout);
    filterPipeline = std::make_shared<pumex::ComputePipeline>(pipelineCache, filterPipelineLayout);
    filterPipeline->shaderStage = { VK_SHADER_STAGE_COMPUTE_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/crowd_filter_instances.comp.spv")), "main" };

    filterDescriptorSet = std::make_shared<pumex::DescriptorSet>(filterDescriptorSetLayout, filterDescriptorPool, 3);
    filterDescriptorSet->setDescriptor(0, cameraUbo);
    filterDescriptorSet->setDescriptor(1, positionSbo);
    filterDescriptorSet->setDescriptor(2, instanceSbo);
    filterDescriptorSet->setDescriptor(3, skeletalAssetBuffer->getTypeBuffer(MAIN_RENDER_MASK));
    filterDescriptorSet->setDescriptor(4, skeletalAssetBuffer->getLodBuffer(MAIN_RENDER_MASK));
    filterDescriptorSet->setDescriptor(5, instancedResults->getResults(MAIN_RENDER_MASK));
    filterDescriptorSet->setDescriptor(6, instancedResults->getOffsetValues(MAIN_RENDER_MASK));

    timeStampQueryPool = std::make_shared<pumex::QueryPool>(VK_QUERY_TYPE_TIMESTAMP,4 * MAX_SURFACES);

    // initializing data
    float fullArea = (maxArea.x - minArea.x) * (maxArea.y - minArea.y);
    unsigned int objectQuantity = (unsigned int)floor(objectDensity * fullArea / 1000000.0f);

    std::uniform_real_distribution<float>   randomX(minArea.x, maxArea.x);
    std::uniform_real_distribution<float>   randomY(minArea.y, maxArea.y);
    std::uniform_int_distribution<uint32_t> randomType(0, mainObjectTypeID.size() - 1);
    std::uniform_real_distribution<float>   randomAnimationOffset(0.0f, 5.0f);

    // each object type has its own number of material variants
    std::vector<std::uniform_int_distribution<uint32_t>> randomMaterialVariant;
    for (uint32_t i = 0; i < materialVariantCount.size(); ++i)
      randomMaterialVariant.push_back(std::uniform_int_distribution<uint32_t>(0, materialVariantCount[i] - 1));

    uint32_t humanID = 0;
    uint32_t clothID = 0;
    for (unsigned int i = 0; i<objectQuantity; ++i)
    {
      humanID++;
      ObjectData human;
        human.kinematic.position    = glm::vec3( randomX(randomEngine), randomY(randomEngine), 0.0f );
        human.kinematic.orientation = glm::angleAxis(randomRotation(randomEngine), glm::vec3(0.0f, 0.0f, 1.0f));
        human.animation             = randomAnimation(randomEngine);
        human.kinematic.velocity    =  glm::rotate(human.kinematic.orientation, glm::vec3(0, -1, 0)) * animationSpeed[human.animation];
        human.animationOffset       = randomAnimationOffset(randomEngine);
        human.typeID                = mainObjectTypeID[randomType(randomEngine)];
        human.materialVariant       = randomMaterialVariant[human.typeID](randomEngine);
        human.time2NextTurn         = randomTime2NextTurn(randomEngine);
      updateData.people.insert({ humanID, human });

      auto clothPair = clothVariants.equal_range(skeletalAssetBuffer->getTypeName(human.typeID));
      auto clothCount = std::distance(clothPair.first, clothPair.second);
      if (clothCount > 0)
      {

        uint32_t clothIndex = i % clothCount;
        std::advance(clothPair.first, clothIndex);
        for( const auto& c : clothPair.first->second )
        {
          clothID++;
          ObjectData cloth;
            cloth.typeID          = skeletalAssetBuffer->getTypeID(c);
            cloth.materialVariant = 0;
            cloth.ownerID         = humanID;
          updateData.clothes.insert({ clothID, cloth });
        }
      }
    }

    std::string fullFontFileName = viewerSh->getFullFilePath("fonts/DejaVuSans.ttf");
    fontDefault                  = std::make_shared<pumex::Font>(fullFontFileName, glm::uvec2(1024,1024), 24, texturesAllocator, buffersAllocator);
    textDefault                  = std::make_shared<pumex::Text>(fontDefault, buffersAllocator);
    fontSmall                    = std::make_shared<pumex::Font>(fullFontFileName, glm::uvec2(512,512), 16, texturesAllocator, buffersAllocator);
    textSmall                    = std::make_shared<pumex::Text>(fontSmall, buffersAllocator);

    
    textCameraUbo = std::make_shared<pumex::UniformBufferPerSurface<pumex::Camera>>(buffersAllocator);
    std::vector<pumex::DescriptorSetLayoutBinding> textLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_GEOMETRY_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    textDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(textLayoutBindings);
    textDescriptorPool = std::make_shared<pumex::DescriptorPool>(6 * MAX_SURFACES, textLayoutBindings);
    // building pipeline layout
    textPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    textPipelineLayout->descriptorSetLayouts.push_back(textDescriptorSetLayout);
    textPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, textPipelineLayout, defaultRenderPass, 0);
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
    textPipeline->depthTestEnable  = VK_FALSE;
    textPipeline->depthWriteEnable = VK_FALSE;
    textPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/text_draw.vert.spv")), "main" },
      { VK_SHADER_STAGE_GEOMETRY_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/text_draw.geom.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/text_draw.frag.spv")), "main" }
    };
    textPipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    textDescriptorSet = std::make_shared<pumex::DescriptorSet>(textDescriptorSetLayout, textDescriptorPool, 3);
    textDescriptorSet->setDescriptor(0, textCameraUbo);
    textDescriptorSet->setDescriptor(1, fontDefault->fontTexture);

    textDescriptorSetSmall = std::make_shared<pumex::DescriptorSet>(textDescriptorSetLayout, textDescriptorPool, 3);
    textDescriptorSetSmall->setDescriptor(0, textCameraUbo);
    textDescriptorSetSmall->setDescriptor(1, fontSmall->fontTexture);


    updateData.cameraPosition              = glm::vec3(0.0f, 0.0f, 0.0f);
    updateData.cameraGeographicCoordinates = glm::vec2(0.0f, 0.0f);
    updateData.cameraDistance              = 1.0f;
    updateData.leftMouseKeyPressed         = false;
    updateData.rightMouseKeyPressed        = false;
    updateData.moveForward                 = false;
    updateData.moveBackward                = false;
    updateData.moveLeft                    = false;
    updateData.moveRight                   = false;
    updateData.moveUp                      = false;
    updateData.moveDown                    = false;
    updateData.moveFast                    = false;

    updateData.measureTime                 = true;
  }

  void surfaceSetup(std::shared_ptr<pumex::Surface> surface)
  {
    pumex::Device* devicePtr           = surface->device.lock().get();
    pumex::CommandPool* commandPoolPtr = surface->commandPool.get();

    myCmdBuffer[surface.get()] = std::make_shared<pumex::CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, devicePtr, commandPoolPtr, surface->getImageCount());

    pipelineCache->validate(devicePtr);

    skeletalAssetBuffer->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
    instancedResults->validate(surface.get());
    materialSet->validate(devicePtr, commandPoolPtr, surface->presentationQueue);

    instancedRenderDescriptorSetLayout->validate(devicePtr);
    instancedRenderDescriptorPool->validate(devicePtr);
    instancedRenderPipelineLayout->validate(devicePtr);
    instancedRenderPipeline->validate(devicePtr);

    filterDescriptorSetLayout->validate(devicePtr);
    filterDescriptorPool->validate(devicePtr);
    filterPipelineLayout->validate(devicePtr);
    filterPipeline->validate(devicePtr);

    textDescriptorSetLayout->validate(devicePtr);
    textDescriptorPool->validate(devicePtr);
    textPipelineLayout->validate(devicePtr);
    textPipeline->validate(devicePtr);

    timeStampQueryPool->validate(surface.get());
  }


  void processInput(std::shared_ptr<pumex::Surface> surface )
  {
    std::shared_ptr<pumex::Window>  windowSh  = surface->window.lock();

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
        case pumex::InputEvent::W:     updateData.moveForward  = true; break;
        case pumex::InputEvent::S:     updateData.moveBackward = true; break;
        case pumex::InputEvent::A:     updateData.moveLeft     = true; break;
        case pumex::InputEvent::D:     updateData.moveRight    = true; break;
        case pumex::InputEvent::Q:     updateData.moveUp       = true; break;
        case pumex::InputEvent::Z:     updateData.moveDown     = true; break;
        case pumex::InputEvent::SHIFT: updateData.moveFast     = true; break;
        case pumex::InputEvent::T: updateData.measureTime = !updateData.measureTime; break;
        }
        break;
      case pumex::InputEvent::KEYBOARD_KEY_RELEASED:
        switch(m.key)
        {
        case pumex::InputEvent::W:     updateData.moveForward  = false; break;
        case pumex::InputEvent::S:     updateData.moveBackward = false; break;
        case pumex::InputEvent::A:     updateData.moveLeft     = false; break;
        case pumex::InputEvent::D:     updateData.moveRight    = false; break;
        case pumex::InputEvent::Q:     updateData.moveUp       = false; break;
        case pumex::InputEvent::Z:     updateData.moveDown     = false; break;
        case pumex::InputEvent::SHIFT: updateData.moveFast     = false; break;
        }
        break;
      }
    }

    uint32_t updateIndex = viewer.lock()->getUpdateIndex();
    RenderData& uData = renderData[updateIndex];
    uData.prevCameraGeographicCoordinates = updateData.cameraGeographicCoordinates;
    uData.prevCameraDistance              = updateData.cameraDistance;
    uData.prevCameraPosition              = updateData.cameraPosition;

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

    float camSpeed = 0.2f;
    if (updateData.moveFast)
      camSpeed = 1.0f;
    glm::vec3 forward = glm::vec3(cos(updateData.cameraGeographicCoordinates.x * 3.1415f / 180.0f), sin(updateData.cameraGeographicCoordinates.x * 3.1415f / 180.0f), 0) * 0.2f;
    glm::vec3 right   = glm::vec3(cos((updateData.cameraGeographicCoordinates.x + 90.0f) * 3.1415f / 180.0f), sin((updateData.cameraGeographicCoordinates.x + 90.0f) * 3.1415f / 180.0f), 0) * 0.2f;
    glm::vec3 up      = glm::vec3(0.0f, 0.0f, 1.0f);
    if (updateData.moveForward)
      updateData.cameraPosition -= forward * camSpeed;
    if (updateData.moveBackward)
      updateData.cameraPosition += forward * camSpeed;
    if (updateData.moveLeft)
      updateData.cameraPosition -= right * camSpeed;
    if (updateData.moveRight)
      updateData.cameraPosition += right * camSpeed;
    if (updateData.moveUp)
      updateData.cameraPosition += up * camSpeed;
    if (updateData.moveDown)
      updateData.cameraPosition -= up * camSpeed;

    if (measureTime != updateData.measureTime)
    {
      for (auto& cb : myCmdBuffer)
        cb.second->setDirty(UINT32_MAX);
      measureTime = updateData.measureTime;
    }

    uData.cameraGeographicCoordinates = updateData.cameraGeographicCoordinates;
    uData.cameraDistance              = updateData.cameraDistance;
    uData.cameraPosition              = updateData.cameraPosition;
  }

  void update(double timeSinceStart, double updateStep)
  {
    // update people positions and state
    std::vector< std::unordered_map<uint32_t, ObjectData>::iterator > iters;
    for (auto it = updateData.people.begin(); it != updateData.people.end(); ++it)
      iters.push_back(it);
    tbb::parallel_for
    (
      tbb::blocked_range<size_t>(0,iters.size()),
      [=](const tbb::blocked_range<size_t>& r)
      {
        for (size_t i = r.begin(); i != r.end(); ++i)
          updateHuman(iters[i]->second, timeSinceStart, updateStep);
      }
    );
    // send UpdateData to RenderData
    uint32_t updateIndex = viewer.lock()->getUpdateIndex();

    std::unordered_map<uint32_t, uint32_t> humanIndexByID;
    renderData[updateIndex].people.resize(0);
    for (auto it = updateData.people.begin(); it != updateData.people.end(); ++it)
    {
      humanIndexByID.insert({ it->first,(uint32_t)renderData[updateIndex].people.size() });
      renderData[updateIndex].people.push_back(it->second);
    }
    renderData[updateIndex].clothes.resize(0);
    renderData[updateIndex].clothOwners.resize(0);
    for (auto it = updateData.clothes.begin(); it != updateData.clothes.end(); ++it)
    {
      renderData[updateIndex].clothes.push_back(it->second);
      renderData[updateIndex].clothOwners.push_back(humanIndexByID[it->second.ownerID]);
    }
  }

  inline void updateHuman( ObjectData& human, float timeSinceStart, float updateStep)
  {
    // change rotation, animation and speed if bot requires it
    if (human.time2NextTurn < 0.0f)
    {
      human.kinematic.orientation = glm::angleAxis(randomRotation(randomEngine), glm::vec3(0.0f, 0.0f, 1.0f));
      human.animation             = randomAnimation(randomEngine);
      human.kinematic.velocity    = glm::rotate(human.kinematic.orientation, glm::vec3(0, -1, 0)) * animationSpeed[human.animation];
      human.time2NextTurn         = randomTime2NextTurn(randomEngine);
    }
    else
      human.time2NextTurn -= updateStep;

    // calculate new position
    human.kinematic.position += human.kinematic.velocity * updateStep;

    // change direction if bot is leaving designated area
    bool isOutside[] =
    {
      human.kinematic.position.x < minArea.x ,
      human.kinematic.position.x > maxArea.x ,
      human.kinematic.position.y < minArea.y ,
      human.kinematic.position.y > maxArea.y
    };
    if (isOutside[0] || isOutside[1] || isOutside[2] || isOutside[3])
    {
      human.kinematic.position.x = std::max(human.kinematic.position.x, minArea.x);
      human.kinematic.position.x = std::min(human.kinematic.position.x, maxArea.x);
      human.kinematic.position.y = std::max(human.kinematic.position.y, minArea.y);
      human.kinematic.position.y = std::min(human.kinematic.position.y, maxArea.y);

      glm::mat4 rotationMatrix = glm::mat4_cast(human.kinematic.orientation);
      glm::vec4 direction = rotationMatrix * glm::rotate(glm::mat4(), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f))  * glm::vec4(1, 0, 0, 1);// MakeHuman models are rotated looking at Y=-1, we have to rotate it
      if (isOutside[0] || isOutside[1])
        direction.x *= -1.0f;
      if (isOutside[2] || isOutside[3])
        direction.y *= -1.0f;
      direction                   = glm::rotate(glm::mat4(), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f))  * direction;

      human.kinematic.orientation = glm::angleAxis(atan2f(direction.y, direction.x), glm::vec3(0.0f, 0.0f, 1.0f));
      human.kinematic.velocity    = glm::rotate(human.kinematic.orientation, glm::vec3(0, -1, 0)) * animationSpeed[human.animation];
      human.time2NextTurn         = randomTime2NextTurn(randomEngine);
    }
  }

  void prepareCameraForRendering(std::shared_ptr<pumex::Surface> surface)
  {
    uint32_t renderIndex = viewer.lock()->getRenderIndex();
    const RenderData& rData = renderData[renderIndex];

    float deltaTime = pumex::inSeconds(viewer.lock()->getRenderTimeDelta());
    float renderTime = pumex::inSeconds(viewer.lock()->getUpdateTime() - viewer.lock()->getApplicationStartTime()) + deltaTime;

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

    glm::mat4 viewMatrix = slaveViewMatrix[surface->getID()] * glm::lookAt(realEye, realCenter, glm::vec3(0, 0, 1));

    pumex::Camera camera;
    camera.setViewMatrix(viewMatrix);
    camera.setObserverPosition(realEye);
    camera.setTimeSinceStart(renderTime);
    uint32_t renderWidth = surface->swapChainSize.width;
    uint32_t renderHeight = surface->swapChainSize.height;
    camera.setProjectionMatrix(glm::perspective(glm::radians(60.0f), (float)renderWidth / (float)renderHeight, 0.1f, 10000.0f));
    cameraUbo->set(surface.get(), camera);

    pumex::Camera textCamera;
    textCamera.setProjectionMatrix(glm::ortho(0.0f, (float)renderWidth, 0.0f, (float)renderHeight), false);
    textCameraUbo->set(surface.get(), textCamera);
  }

  void prepareBuffersForRendering()
  {
    pumex::HPClock::time_point thisFrameStart = pumex::HPClock::now();
    double fpsValue = 1.0 / pumex::inSeconds(thisFrameStart - lastFrameStart);
    lastFrameStart = thisFrameStart;

    uint32_t renderIndex = viewer.lock()->getRenderIndex();
    const RenderData& rData = renderData[renderIndex];

    float deltaTime  = pumex::inSeconds(viewer.lock()->getRenderTimeDelta());
    float renderTime = pumex::inSeconds(viewer.lock()->getUpdateTime() - viewer.lock()->getApplicationStartTime()) + deltaTime;

    std::vector<uint32_t> typeCount(skeletalAssetBuffer->getNumTypesID());
    std::fill(typeCount.begin(), typeCount.end(), 0);
    // compute how many instances of each type there is
    for (uint32_t i = 0; i < rData.people.size(); ++i)
      typeCount[rData.people[i].typeID]++;
    for (uint32_t i = 0; i < rData.clothes.size(); ++i)
      typeCount[rData.clothes[i].typeID]++;

    instancedResults->prepareBuffers(typeCount);

    std::vector<PositionData> positionData;
    std::vector<InstanceData> instanceData;
    std::vector<uint32_t> animIndex;
    std::vector<float> animOffset;
    for (auto it = rData.people.begin(); it != rData.people.end(); ++it)
    {
      uint32_t index = positionData.size();
      PositionData position(pumex::extrapolate(it->kinematic, deltaTime));

      positionData.emplace_back(position);
      instanceData.emplace_back(InstanceData(index, it->typeID, it->materialVariant, 1));

      animIndex.emplace_back(it->animation);
      animOffset.emplace_back(it->animationOffset);
    }

    // calculate bone matrices for the people
    tbb::parallel_for
    (
      tbb::blocked_range<size_t>(0, positionData.size()),
      [&](const tbb::blocked_range<size_t>& r)
      {
        for (size_t i = r.begin(); i != r.end(); ++i)
        {
          pumex::Animation& anim = animations[animIndex[i]];
          pumex::Skeleton&  skel = skeletons[instanceData[i].typeID];

          uint32_t numAnimChannels = anim.channels.size();
          uint32_t numSkelBones = skel.bones.size();
          SkelAnimKey saKey(instanceData[i].typeID, animIndex[i]);

          auto bmit = skelAnimBoneMapping.find(saKey);
          if (bmit == skelAnimBoneMapping.end())
          {
            std::vector<uint32_t> boneChannelMapping(numSkelBones);
            for (uint32_t boneIndex = 0; boneIndex < numSkelBones; ++boneIndex)
            {
              auto it = anim.invChannelNames.find(skel.boneNames[boneIndex]);
              boneChannelMapping[boneIndex] = (it != anim.invChannelNames.end()) ? it->second : UINT32_MAX;
            }
            bmit = skelAnimBoneMapping.insert({ saKey, boneChannelMapping }).first;
          }

          std::vector<glm::mat4> localTransforms(MAX_BONES);
          std::vector<glm::mat4> globalTransforms(MAX_BONES);

          const auto& boneChannelMapping = bmit->second;
          anim.calculateLocalTransforms(renderTime + animOffset[i], localTransforms.data(), numAnimChannels);
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
            positionData[i].bones[boneIndex] = globalTransforms[boneIndex] * skel.bones[boneIndex].offsetMatrix;

        }
      }
    );

    uint32_t ii = 0;
    for (auto it = rData.clothes.begin(); it != rData.clothes.end(); ++it, ++ii)
    {
      instanceData.emplace_back(InstanceData(rData.clothOwners[ii], it->typeID, it->materialVariant, 0));
    }
    positionSbo->set(positionData);
    instanceSbo->set(instanceData);

    std::wstringstream stream;
    stream << "FPS : " << std::fixed << std::setprecision(1) << fpsValue;
    textDefault->setText(viewer.lock()->getSurface(0), 0, glm::vec2(30, 28), glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), stream.str());
  }

  void draw( std::shared_ptr<pumex::Surface> surface )
  {
    pumex::Surface*     surfacePtr     = surface.get();
    pumex::Device*      devicePtr      = surface->device.lock().get();
    pumex::CommandPool* commandPoolPtr = surface->commandPool.get();
    uint32_t            renderIndex    = surface->viewer.lock()->getRenderIndex();
    const RenderData&   rData          = renderData[renderIndex];
    unsigned long long  frameNumber    = surface->viewer.lock()->getFrameNumber();
    uint32_t            activeIndex    = frameNumber % 3;
    uint32_t            renderWidth    = surface->swapChainSize.width;
    uint32_t            renderHeight   = surface->swapChainSize.height;

    fontDefault->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
    textDefault->setActiveIndex(activeIndex);
    textDefault->validate(surfacePtr);

    fontSmall->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
    if (measureTime)
    {
      std::lock_guard<std::mutex> lock(measureMutex);
      std::wstringstream stream;
      stream << "Process input          : " << std::fixed << std::setprecision(3) << 1000.0 * times[1010] << " ms";
      textSmall->setText(surfacePtr, 1010, glm::vec2(30, 58), glm::vec4(0.0f, 0.9f, 0.0f, 1.0f), stream.str());

      stream.str(L"");
      stream << "Update                    : " << std::fixed << std::setprecision(3) << 1000.0 * times[1020] << " ms";
      textSmall->setText(surfacePtr, 1020, glm::vec2(30, 78), glm::vec4(0.0f, 0.9f, 0.0f, 1.0f), stream.str());

      stream.str(L"");
      stream << "Prepare buffers       : " << std::fixed << std::setprecision(3) << 1000.0 * times[2010] << " ms";
      textSmall->setText(surfacePtr, 2010, glm::vec2(30, 118), glm::vec4(0.9f, 0.0f, 0.0f, 1.0f), stream.str());

      stream.str(L"");
      stream << "Begin frame            : " << std::fixed << std::setprecision(3) << 1000.0 * times[2020+surfacePtr->getID()] << " ms";
      textSmall->setText(surfacePtr, 2020, glm::vec2(30, 138), glm::vec4(0.9f, 0.0f, 0.0f, 1.0f), stream.str());

      stream.str(L"");
      stream << "Draw frame             : " << std::fixed << std::setprecision(3) << 1000.0 * times[2030 + surfacePtr->getID()] << " ms";
      textSmall->setText(surfacePtr, 2030, glm::vec2(30, 158), glm::vec4(0.9f, 0.0f, 0.0f, 1.0f), stream.str());

      stream.str(L"");
      stream << "End frame               : " << std::fixed << std::setprecision(3) << 1000.0 * times[2040 + surfacePtr->getID()] << " ms";
      textSmall->setText(surfacePtr, 2040, glm::vec2(30, 178), glm::vec4(0.9f, 0.0f, 0.0f, 1.0f), stream.str());

      float timeStampPeriod = devicePtr->physical.lock()->properties.limits.timestampPeriod / 1000000.0f;
      std::vector<uint64_t> queryResults;
      // We use swapChainImageIndex to get the time measurments from previous frame - timeStampQueryPool works like circular buffer
      queryResults = timeStampQueryPool->getResults(surfacePtr, ((activeIndex + 2) % 3) * 4, 4, 0);
      // exclude timer overflows
      if (queryResults[1] > queryResults[0])
      {
        stream.str(L"");
        stream << "GPU LOD compute shader : " << std::fixed << std::setprecision(3) << (queryResults[1] - queryResults[0]) * timeStampPeriod << " ms";
        textSmall->setText(surfacePtr, 3010, glm::vec2(30, 218), glm::vec4(0.8f, 0.8f, 0.0f, 1.0f), stream.str());
      }

      // exclude timer overflows
      if (queryResults[3] > queryResults[2])
      {
        stream.str(L"");
        stream << "GPU draw shader        : " << std::fixed << std::setprecision(3) << (queryResults[3] - queryResults[2]) * timeStampPeriod << " ms";
        textSmall->setText(surfacePtr, 3020, glm::vec2(30, 238), glm::vec4(0.8f, 0.8f, 0.0f, 1.0f), stream.str());
      }
    }
    textSmall->setActiveIndex(activeIndex);
    textSmall->validate(surfacePtr);

    cameraUbo->validate(surfacePtr);
    positionSbo->setActiveIndex(activeIndex);
    positionSbo->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
    instanceSbo->setActiveIndex(activeIndex);
    instanceSbo->validate(devicePtr, commandPoolPtr, surface->presentationQueue);

    instancedResults->setActiveIndex(activeIndex);
    instancedResults->validate(surfacePtr);

    instancedRenderDescriptorSet->setActiveIndex(activeIndex);
    instancedRenderDescriptorSet->validate(surfacePtr);
    filterDescriptorSet->setActiveIndex(activeIndex);
    filterDescriptorSet->validate(surfacePtr);

    textCameraUbo->validate(surfacePtr);
    textDescriptorSet->setActiveIndex(activeIndex);
    textDescriptorSet->validate(surfacePtr);

    textDescriptorSetSmall->setActiveIndex(activeIndex);
    textDescriptorSetSmall->validate(surfacePtr);

    auto& currentCmdBuffer = myCmdBuffer[surfacePtr];
    currentCmdBuffer->setActiveIndex(activeIndex);
    if (!currentCmdBuffer->isValid(activeIndex))
    {
      currentCmdBuffer->cmdBegin();
      timeStampQueryPool->reset(surfacePtr, currentCmdBuffer, activeIndex * 4, 4);

      std::vector<pumex::DescriptorSetValue> resultsBuffer;
      instancedResults->getResults(MAIN_RENDER_MASK)->getDescriptorSetValues(surface->surface, activeIndex, resultsBuffer);
      uint32_t drawCount = instancedResults->getDrawCount(MAIN_RENDER_MASK);

      if(measureTime)
        timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 4 + 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

      currentCmdBuffer->cmdBindPipeline(filterPipeline.get());
      currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, surfacePtr, filterPipelineLayout.get(), 0, filterDescriptorSet.get());
      uint32_t instanceCount = rData.people.size() + rData.clothes.size();
      currentCmdBuffer->cmdDispatch(instanceCount / 16 + ((instanceCount % 16 > 0) ? 1 : 0), 1, 1);

      pumex::PipelineBarrier afterComputeBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, surface->presentationQueueFamilyIndex, surface->presentationQueueFamilyIndex, resultsBuffer[0].bufferInfo);
      currentCmdBuffer->cmdPipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, afterComputeBarrier);

      if (measureTime)
        timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 4 + 1, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);

      std::vector<VkClearValue> clearValues = { pumex::makeColorClearValue(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), pumex::makeDepthStencilClearValue(1.0f, 0) };
      currentCmdBuffer->cmdBeginRenderPass(surfacePtr, defaultRenderPass.get(), surface->frameBuffer.get(), surface->getImageIndex(),  pumex::makeVkRect2D(0, 0, renderWidth, renderHeight), clearValues);
      currentCmdBuffer->cmdSetViewport(0, { pumex::makeViewport(0, 0, renderWidth, renderHeight, 0.0f, 1.0f) });
      currentCmdBuffer->cmdSetScissor(0, { pumex::makeVkRect2D(0, 0, renderWidth, renderHeight) });

      if (measureTime)
        timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 4 + 2, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);

      currentCmdBuffer->cmdBindPipeline(instancedRenderPipeline.get());
      currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePtr, instancedRenderPipelineLayout.get(), 0, instancedRenderDescriptorSet.get());
      skeletalAssetBuffer->cmdBindVertexIndexBuffer(devicePtr, currentCmdBuffer.get(), MAIN_RENDER_MASK, 0);
      if (devicePtr->physical.lock()->features.multiDrawIndirect == 1)
        currentCmdBuffer->cmdDrawIndexedIndirect(resultsBuffer[0].bufferInfo.buffer, resultsBuffer[0].bufferInfo.offset, drawCount, sizeof(pumex::DrawIndexedIndirectCommand));
      else
      {
        for (uint32_t i = 0; i < drawCount; ++i)
          currentCmdBuffer->cmdDrawIndexedIndirect(resultsBuffer[0].bufferInfo.buffer, resultsBuffer[0].bufferInfo.offset + i * sizeof(pumex::DrawIndexedIndirectCommand), 1, sizeof(pumex::DrawIndexedIndirectCommand));
      }
      if (measureTime)
        timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 4 + 3, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

      currentCmdBuffer->cmdBindPipeline(textPipeline.get());
      currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePtr, textPipelineLayout.get(), 0, textDescriptorSet.get());
      textDefault->cmdDraw(surfacePtr, currentCmdBuffer);

      if (measureTime)
      {
        currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePtr, textPipelineLayout.get(), 0, textDescriptorSetSmall.get());
        textSmall->cmdDraw(surfacePtr, currentCmdBuffer);
      }

      currentCmdBuffer->cmdEndRenderPass();
      currentCmdBuffer->cmdEnd();
    }
    currentCmdBuffer->queueSubmit(surface->presentationQueue, { surface->imageAvailableSemaphore }, { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }, { surface->renderCompleteSemaphore }, VK_NULL_HANDLE);
  }

  void setSlaveViewMatrix(uint32_t index, const glm::mat4& matrix)
  {
    slaveViewMatrix[index] = matrix;
  }
  pumex::HPClock::time_point now()
  {
    if (!measureTime)
      return pumex::HPClock::time_point();
    return pumex::HPClock::now();
  }
  pumex::HPClock::time_point setTime(uint32_t marker, pumex::HPClock::time_point& startPoint)
  {
    if (!measureTime)
      return pumex::HPClock::time_point();
    
    std::lock_guard<std::mutex> lock(measureMutex);
    auto result = pumex::HPClock::now();
    times[marker] = pumex::inSeconds(result - startPoint);
    return result;
  }
};


int main(int argc, char * argv[])
{
  SET_LOG_INFO;
  args::ArgumentParser parser("pumex example : multithreaded crowd rendering on more than one window");
  args::HelpFlag       help(parser, "help", "display this help menu", {'h', "help"});
  args::Flag           enableDebugging(parser, "debug", "enable Vulkan debugging", {'d'});
  args::Flag           useFullScreen(parser, "fullscreen", "create fullscreen window", {'f'});
  args::Flag           renderVRwindows(parser, "vrwindows", "create two halfscreen windows for VR", { 'v' });
  args::Flag           render3windows(parser, "three_windows", "render in three windows", {'t'});
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
  LOG_INFO << "Crowd rendering";
  if (enableDebugging)
    LOG_INFO << " : Vulkan debugging enabled";
  LOG_INFO << std::endl;

  const std::vector<std::string> requestDebugLayers = { { "VK_LAYER_LUNARG_standard_validation" } };
  pumex::ViewerTraits viewerTraits{ "Crowd rendering application", enableDebugging, requestDebugLayers, 50 };
  viewerTraits.debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT;// | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;

  std::shared_ptr<pumex::Viewer> viewer;
  try
  {
    viewer = std::make_shared<pumex::Viewer>(viewerTraits);

    std::vector<pumex::WindowTraits> windowTraits;
    if (render3windows)
    {
      windowTraits.emplace_back( pumex::WindowTraits{ 0, 30,   100, 512, 384, pumex::WindowTraits::WINDOW, "Crowd rendering 1" } );
      windowTraits.emplace_back( pumex::WindowTraits{ 0, 570,  100, 512, 384, pumex::WindowTraits::WINDOW, "Crowd rendering 2" } );
      windowTraits.emplace_back( pumex::WindowTraits{ 0, 1110, 100, 512, 384, pumex::WindowTraits::WINDOW, "Crowd rendering 3" } );
    }
    else if (renderVRwindows)
    {
      windowTraits.emplace_back(pumex::WindowTraits{ 0, 0, 0, 100, 100, pumex::WindowTraits::HALFSCREEN_LEFT, "Crowd rendering L" });
      windowTraits.emplace_back(pumex::WindowTraits{ 0, 100, 0, 100, 100, pumex::WindowTraits::HALFSCREEN_RIGHT, "Crowd rendering R" });
    }
    else
    {
      windowTraits.emplace_back(pumex::WindowTraits{ 0, 100, 100, 640, 480, useFullScreen ? pumex::WindowTraits::FULLSCREEN : pumex::WindowTraits::WINDOW, "Crowd rendering" });
    }

    std::vector<float> queuePriorities;
    queuePriorities.resize(windowTraits.size(), 0.75f);
    std::vector<pumex::QueueTraits> requestQueues    = { { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, queuePriorities } };
    std::vector<const char*> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device            = viewer->addDevice(0, requestQueues, requestDeviceExtensions);
    CHECK_LOG_THROW(!device->isValid(), "Cannot create logical device with requested parameters" );

    std::vector<std::shared_ptr<pumex::Window>> windows;
    for (const auto& t : windowTraits)
      windows.push_back(pumex::Window::createWindow(t));

    std::vector<pumex::FrameBufferImageDefinition> frameBufferDefinitions =
    {
      { pumex::atSurface, VK_FORMAT_B8G8R8A8_UNORM,    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,         VK_IMAGE_ASPECT_COLOR_BIT,                               VK_SAMPLE_COUNT_1_BIT },
      { pumex::atDepth,   VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, VK_SAMPLE_COUNT_1_BIT }
    };
    // allocate 24 MB for frame buffers ( actually only depth buffer will be allocated )
    std::shared_ptr<pumex::DeviceMemoryAllocator> frameBufferAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 24 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
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

    pumex::SurfaceTraits surfaceTraits{ 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    surfaceTraits.definePresentationQueue(pumex::QueueTraits{ VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0,{ 0.75f } });
    surfaceTraits.setDefaultRenderPass(renderPass);
    surfaceTraits.setFrameBufferImages(frameBufferImages);

    std::shared_ptr<CrowdApplicationData> applicationData = std::make_shared<CrowdApplicationData>(viewer);
    applicationData->defaultRenderPass = renderPass;
    applicationData->setup(glm::vec3(-25, -25, 0), glm::vec3(25, 25, 0), 200000);

    if (render3windows)
    {
      applicationData->setSlaveViewMatrix(0, glm::rotate(glm::mat4(), glm::radians(-75.16f), glm::vec3(0.0f, 1.0f, 0.0f)));
      applicationData->setSlaveViewMatrix(1, glm::mat4());
      applicationData->setSlaveViewMatrix(2, glm::rotate(glm::mat4(), glm::radians(75.16f), glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    else if(renderVRwindows)
    {
      applicationData->setSlaveViewMatrix(0, glm::translate(glm::mat4(), glm::vec3( 0.03f, 0.0f, 0.0f)));
      applicationData->setSlaveViewMatrix(1, glm::translate(glm::mat4(), glm::vec3( -0.03f, 0.0f, 0.0f)));
    }
    else
    {
      applicationData->setSlaveViewMatrix(0, glm::mat4());
    }

    std::vector<std::shared_ptr<pumex::Surface>> surfaces;
    for (auto win : windows)
      surfaces.push_back(viewer->addSurface(win, device, surfaceTraits));
    for (auto surf : surfaces)
      applicationData->surfaceSetup(surf);

    // Making the update graph
    // The update in this example is "almost" singlethreaded. 
    // In more complicated scenarios update should be also divided into advanced update graph.
    // Consider make_edge() in update graph :
    // viewer->startUpdateGraph should point to all root nodes.
    // All leaf nodes should point to viewer->endUpdateGraph.
    tbb::flow::continue_node< tbb::flow::continue_msg > update(viewer->updateGraph, [=](tbb::flow::continue_msg)
    {
      auto inputBeginTime = applicationData->now();
      for (auto surf : surfaces)
        applicationData->processInput(surf);
      auto updateBeginTime = applicationData->setTime(1010, inputBeginTime);
      applicationData->update(pumex::inSeconds( viewer->getUpdateTime() - viewer->getApplicationStartTime() ), pumex::inSeconds(viewer->getUpdateDuration()));
      applicationData->setTime(1020, updateBeginTime);
    });

    tbb::flow::make_edge(viewer->startUpdateGraph, update);
    tbb::flow::make_edge(update, viewer->endUpdateGraph);

    // Making the render graph.
    // This one is also "single threaded" ( look at the make_edge() calls ), but presents a method of connecting graph nodes.
    // Consider make_edge() in render graph :
    // viewer->startRenderGraph should point to all root nodes.
    // All leaf nodes should point to viewer->endRenderGraph.
    tbb::flow::continue_node< tbb::flow::continue_msg > prepareBuffers(viewer->renderGraph, [=](tbb::flow::continue_msg) 
    { 
      auto t = applicationData->now();
      applicationData->prepareBuffersForRendering();
      applicationData->setTime(2010, t);
    });
    std::vector<tbb::flow::continue_node< tbb::flow::continue_msg >> startSurfaceFrame, drawSurfaceFrame, endSurfaceFrame;
    for (auto& surf : surfaces)
    {
      startSurfaceFrame.emplace_back(tbb::flow::continue_node< tbb::flow::continue_msg >(viewer->renderGraph, [=](tbb::flow::continue_msg) 
      { 
        auto t = applicationData->now();
        applicationData->prepareCameraForRendering(surf);
        surf->beginFrame(); 
        applicationData->setTime(2020+surf->getID(), t);
      }));
      drawSurfaceFrame.emplace_back(tbb::flow::continue_node< tbb::flow::continue_msg >(viewer->renderGraph, [=](tbb::flow::continue_msg) 
      { 
        auto t = applicationData->now();
        applicationData->draw(surf);
        applicationData->setTime(2030 + surf->getID(), t);
      }));
      endSurfaceFrame.emplace_back(tbb::flow::continue_node< tbb::flow::continue_msg >(viewer->renderGraph, [=](tbb::flow::continue_msg) 
      { 
        auto t = applicationData->now();
        surf->endFrame();
        applicationData->setTime(2040 + surf->getID(), t);
      }));
    }

    tbb::flow::make_edge(viewer->startRenderGraph, prepareBuffers);
    for (uint32_t i = 0; i < startSurfaceFrame.size(); ++i)
    {
      tbb::flow::make_edge(prepareBuffers, startSurfaceFrame[i]);
      tbb::flow::make_edge(startSurfaceFrame[i], drawSurfaceFrame[i]);
      tbb::flow::make_edge(drawSurfaceFrame[i], endSurfaceFrame[i]);
      tbb::flow::make_edge(endSurfaceFrame[i], viewer->endRenderGraph);
    }
    viewer->run();
  }
  catch (const std::exception e)
  {
#if defined(_DEBUG) && defined(_WIN32)
    OutputDebugStringA(e.what());
#endif
  }
  catch (...)
  {
#if defined(_DEBUG) && defined(_WIN32)
    OutputDebugStringA("Unknown error\n");
#endif
  }
  viewer->cleanup();
  FLUSH_LOG;
  return 0;
}

// Small hint : print spir-v in human readable format
// glslangValidator -H instanced_animation.vert -o instanced_animation.vert.spv >>instanced_animation.vert.txt
// glslangValidator -H instanced_animation.frag -o instanced_animation.frag.spv >>instanced_animation.frag.txt
