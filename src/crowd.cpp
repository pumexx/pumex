#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/simd/matrix.h>
#include <gli/gli.hpp>
#include <pumex/Pumex.h>
#include <pumex/AssetLoaderAssimp.h>
#include <tbb/tbb.h>

// Current measurment methods add 4ms to a single frame ( cout lags )
// I suggest using applications such as RenderDoc to measure frame time for now.
//#define CROWD_MEASURE_TIME 1

const uint32_t MAX_BONES = 63;

// Structure holding information about people and objects.
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

  uint32_t                                 renderMethod;
  glm::vec2                                lastMousePos;
  bool                                     leftMouseKeyPressed;
  bool                                     rightMouseKeyPressed;
  bool                                     xKeyPressed;
};


struct RenderData
{
  RenderData()
    : renderMethod{ 1 }
  {
  }
  uint32_t                renderMethod;

  pumex::Kinematic        cameraKinematic;

  std::vector<ObjectData> people;
  std::vector<ObjectData> clothes;
};

struct PositionData
{
  PositionData(const glm::mat4& p)
    : position{p}
  {
  }
  glm::mat4 position;
  glm::mat4 bones[MAX_BONES];
};

struct InstanceData
{
  InstanceData(uint32_t p, uint32_t t, uint32_t m, uint32_t i)
    : positionIndex{ p }, typeID{ t }, materialVariant{ m }, mainInstance {i}
  {
  }
  uint32_t positionIndex;
  uint32_t typeID;
  uint32_t materialVariant;
  uint32_t mainInstance;
};

//struct InstanceDataCPU
//{
//  InstanceDataCPU(uint32_t a, const glm::vec2& p, float r, float s, float t, float o)
//    : animation{ a }, position{ p }, rotation{ r }, speed{ s }, time2NextTurn{ t }, animationOffset{o}
//  {
//  }
//  uint32_t  animation;
//  glm::vec2 position;
//  float     rotation;
//  float     speed;
//  float     time2NextTurn;
//  float     animationOffset;
//};

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

struct ApplicationData
{
  std::weak_ptr<pumex::Viewer>                         viewer;

  UpdateData                                           updateData;
  std::array<RenderData, 3>                            renderData;

//  std::vector<PositionData>                            positionData;
//  std::vector<InstanceData>                            instanceData;
//  pumex::Camera                                        camera;

  glm::vec3                                            minArea;
  glm::vec3                                            maxArea;
  std::vector<pumex::Skeleton>                         skeletons;
  std::vector<pumex::Animation>                        animations;
  std::map<SkelAnimKey, std::vector<uint32_t>>         skelAnimBoneMapping;
  std::vector<float>                                   animationSpeed;

  std::default_random_engine                           randomEngine;
  std::exponential_distribution<float>                 randomTime2NextTurn;
  std::uniform_real_distribution<float>                randomRotation;
  std::uniform_int_distribution<uint32_t>              randomAnimation;

  std::shared_ptr<pumex::AssetBuffer>                  skeletalAssetBuffer;
  std::shared_ptr<pumex::TextureRegistryArray>         textureRegistryArray;
  std::shared_ptr<pumex::MaterialSet<MaterialData>>    materialSet;

  std::shared_ptr<pumex::UniformBuffer<pumex::Camera>>                      cameraUbo;
  std::shared_ptr<pumex::StorageBuffer<PositionData>>                       positionSbo;
  std::shared_ptr<pumex::StorageBuffer<InstanceData>>                       instanceSbo;
  std::shared_ptr<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>  resultsSbo;
  std::shared_ptr<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>  resultsSbo2;
  std::vector<uint32_t>                                                     resultsGeomToType;
  std::shared_ptr<pumex::StorageBuffer<uint32_t>>                           offValuesSbo;

  std::shared_ptr<pumex::RenderPass>                   defaultRenderPass;

  std::shared_ptr<pumex::PipelineCache>                pipelineCache;

  std::shared_ptr<pumex::DescriptorSetLayout>          simpleRenderDescriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>               simpleRenderPipelineLayout;
  std::shared_ptr<pumex::GraphicsPipeline>             simpleRenderPipeline;
  std::shared_ptr<pumex::DescriptorPool>               simpleRenderDescriptorPool;
  std::shared_ptr<pumex::DescriptorSet>                simpleRenderDescriptorSet;

  std::shared_ptr<pumex::DescriptorSetLayout>          instancedRenderDescriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>               instancedRenderPipelineLayout;
  std::shared_ptr<pumex::GraphicsPipeline>             instancedRenderPipeline;
  std::shared_ptr<pumex::DescriptorPool>               instancedRenderDescriptorPool;
  std::shared_ptr<pumex::DescriptorSet>                instancedRenderDescriptorSet;

  std::shared_ptr<pumex::DescriptorSetLayout>          filterDescriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>               filterPipelineLayout;
  std::shared_ptr<pumex::ComputePipeline>              filterPipeline;
  std::shared_ptr<pumex::DescriptorPool>               filterDescriptorPool;
  std::shared_ptr<pumex::DescriptorSet>                filterDescriptorSet;

  std::shared_ptr<pumex::QueryPool>                    timeStampQueryPool;

  double    inputDuration;
  double    updateDuration;
  double    recalcDuration;
  double    drawDuration;

  std::unordered_map<VkDevice,std::shared_ptr<pumex::CommandBuffer>> myCmdBuffer;

  ApplicationData(std::shared_ptr<pumex::Viewer> v)
	  : viewer{ v }, randomTime2NextTurn{ 0.25 }, randomRotation{ -180.0f, 180.0f }
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
      "wmale1_bbox.dae",
      "wmale1_walk.dae",
      "wmale1_walk_easy.dae",
      "wmale1_walk_big_steps.dae",
      "wmale1_run.dae"
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

    std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };
    skeletalAssetBuffer = std::make_shared<pumex::AssetBuffer>();
    skeletalAssetBuffer->registerVertexSemantic(1, vertexSemantic);

    textureRegistryArray = std::make_shared<pumex::TextureRegistryArray>();
    textureRegistryArray->setTargetTexture(0, std::make_shared<pumex::Texture>(gli::texture(gli::target::TARGET_2D_ARRAY, gli::format::FORMAT_RGBA_DXT1_UNORM_BLOCK8, gli::texture::extent_type(2048, 2048, 1), 24, 1, 12), pumex::TextureTraits()));
    std::vector<pumex::TextureSemantic> textureSemantic = { { pumex::TextureSemantic::Diffuse, 0 } };
    materialSet = std::make_shared<pumex::MaterialSet<MaterialData>>(viewerSh, textureRegistryArray, textureSemantic);

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
      "wmale1_lod0.dae", "wmale1_lod1.dae", "wmale1_lod2.dae",
      "wmale2_lod0.dae", "wmale2_lod1.dae", "wmale2_lod2.dae",
      "wmale3_lod0.dae", "wmale3_lod1.dae", "wmale3_lod2.dae",
      "wmale1_cloth1.dae", "", "", // well, I don't have LODded cloths :(
      "wmale1_cloth2.dae", "", "",
      "wmale1_cloth3.dae", "", "",
      "wmale2_cloth1.dae", "", "",
      "wmale2_cloth2.dae", "", "",
      "wmale2_cloth3.dae", "", "",
      "wmale3_cloth1.dae", "", "",
      "wmale3_cloth2.dae", "", "",
      "wmale3_cloth3.dae", "", ""
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
      { "wmale1", { { "body_mat", "young_lightskinned_male_diffuse_1.dds" } } },
      { "wmale1", { { "body_mat", "young_lightskinned_male_diffuse.dds" } } },
      { "wmale2", { { "body_mat", "young_lightskinned_male_diffuse3_1.dds" } } },
      { "wmale2", { { "body_mat", "dragon_female_white.dds" } } },
      { "wmale3", { { "body_mat", "middleage_lightskinned_male_diffuse_1.dds"} } },
      { "wmale3", { { "body_mat", "ork_texture.dds" } } }
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
    materialSet->refreshMaterialStructures();
    std::vector<uint32_t> materialVariantCount(skeletalNames.size()+1);
    for (uint32_t i= 0; i<materialVariantCount.size(); ++i)
      materialVariantCount[i] = materialSet->getMaterialVariantCount(i);

    cameraUbo    = std::make_shared<pumex::UniformBuffer<pumex::Camera>>();
    positionSbo  = std::make_shared<pumex::StorageBuffer<PositionData>>();
    instanceSbo  = std::make_shared<pumex::StorageBuffer<InstanceData>>();
    resultsSbo   = std::make_shared<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    resultsSbo2  = std::make_shared<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>((VkBufferUsageFlagBits)(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    offValuesSbo = std::make_shared<pumex::StorageBuffer<uint32_t>>();

    pipelineCache = std::make_shared<pumex::PipelineCache>();

    std::vector<pumex::DescriptorSetLayoutBinding> simpleRenderLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
      { 6, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }

    };
    simpleRenderDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(simpleRenderLayoutBindings);
    simpleRenderDescriptorPool = std::make_shared<pumex::DescriptorPool>(1, simpleRenderLayoutBindings);
    // building pipeline layout
    simpleRenderPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    simpleRenderPipelineLayout->descriptorSetLayouts.push_back(simpleRenderDescriptorSetLayout);
    simpleRenderPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, simpleRenderPipelineLayout, defaultRenderPass, 0);
    simpleRenderPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("crowd_simple_animation.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("crowd_simple_animation.frag.spv")), "main" }
    };
    simpleRenderPipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, vertexSemantic }
    };
    simpleRenderPipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    simpleRenderPipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    simpleRenderDescriptorSet = std::make_shared<pumex::DescriptorSet>(simpleRenderDescriptorSetLayout, simpleRenderDescriptorPool);
    simpleRenderDescriptorSet->setSource(0, cameraUbo);
    simpleRenderDescriptorSet->setSource(1, positionSbo);
    simpleRenderDescriptorSet->setSource(2, instanceSbo);
    simpleRenderDescriptorSet->setSource(3, materialSet->getTypeBufferDescriptorSetSource());
    simpleRenderDescriptorSet->setSource(4, materialSet->getMaterialVariantBufferDescriptorSetSource());
    simpleRenderDescriptorSet->setSource(5, materialSet->getMaterialDefinitionBufferDescriptorSetSource());
    simpleRenderDescriptorSet->setSource(6, textureRegistryArray->getTargetTexture(0));

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
    instancedRenderDescriptorPool = std::make_shared<pumex::DescriptorPool>(1, instancedRenderLayoutBindings);
    // building pipeline layout
    instancedRenderPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    instancedRenderPipelineLayout->descriptorSetLayouts.push_back(instancedRenderDescriptorSetLayout);
    instancedRenderPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, instancedRenderPipelineLayout, defaultRenderPass, 0);
    instancedRenderPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("crowd_instanced_animation.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("crowd_instanced_animation.frag.spv")), "main" }
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

    instancedRenderDescriptorSet = std::make_shared<pumex::DescriptorSet>(instancedRenderDescriptorSetLayout, instancedRenderDescriptorPool);
    instancedRenderDescriptorSet->setSource(0, cameraUbo);
    instancedRenderDescriptorSet->setSource(1, positionSbo);
    instancedRenderDescriptorSet->setSource(2, instanceSbo);
    instancedRenderDescriptorSet->setSource(3, offValuesSbo);
    instancedRenderDescriptorSet->setSource(4, materialSet->getTypeBufferDescriptorSetSource());
    instancedRenderDescriptorSet->setSource(5, materialSet->getMaterialVariantBufferDescriptorSetSource());
    instancedRenderDescriptorSet->setSource(6, materialSet->getMaterialDefinitionBufferDescriptorSetSource());
    instancedRenderDescriptorSet->setSource(7, textureRegistryArray->getTargetTexture(0));

    std::vector<pumex::DescriptorSetLayoutBinding> filterLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 2, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      { 6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT }
    };
    filterDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(filterLayoutBindings);
    filterDescriptorPool = std::make_shared<pumex::DescriptorPool>(1, filterLayoutBindings);
    // building pipeline layout
    filterPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    filterPipelineLayout->descriptorSetLayouts.push_back(filterDescriptorSetLayout);
    filterPipeline = std::make_shared<pumex::ComputePipeline>(pipelineCache, filterPipelineLayout);
    filterPipeline->shaderStage = { VK_SHADER_STAGE_COMPUTE_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("crowd_filter_instances.comp.spv")), "main" };

    filterDescriptorSet = std::make_shared<pumex::DescriptorSet>(filterDescriptorSetLayout, filterDescriptorPool);
    filterDescriptorSet->setSource(0, skeletalAssetBuffer->getTypeBufferDescriptorSetSource(1));
    filterDescriptorSet->setSource(1, skeletalAssetBuffer->getLODBufferDescriptorSetSource(1));
    filterDescriptorSet->setSource(2, cameraUbo);
    filterDescriptorSet->setSource(3, positionSbo);
    filterDescriptorSet->setSource(4, instanceSbo);
    filterDescriptorSet->setSource(5, resultsSbo);
    filterDescriptorSet->setSource(6, offValuesSbo);

    timeStampQueryPool = std::make_shared<pumex::QueryPool>(VK_QUERY_TYPE_TIMESTAMP,12);

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
        human.kinematic.orientation = glm::quat( randomRotation(randomEngine), glm::vec3(0.0f, 0.0f, 1.0f) );
        human.animation             = randomAnimation(randomEngine);
        glm::mat4 rotationMatrix    = glm::mat4_cast(human.kinematic.orientation);
        glm::vec4 direction4        = rotationMatrix * glm::rotate(glm::mat4(), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f))  * glm::vec4(1, 0, 0, 1);// MakeHuman models are rotated looking at Y=-1, we have to take it into account
        glm::vec3 direction3 (direction4.x/ direction4.w, direction4.y / direction4.w, direction4.z / direction4.w);
        human.kinematic.velocity    = direction3 * (animationSpeed[human.animation] / direction3.length());
        human.animationOffset       = randomAnimationOffset(randomEngine);
        human.typeID                = mainObjectTypeID[randomType(randomEngine)];
        human.materialVariant       = randomMaterialVariant[human.typeID](randomEngine);
        human.time2NextTurn         = randomTime2NextTurn(randomEngine);
      updateData.people.insert({ humanID, human });

//      float rot                      = randomRotation(randomEngine);
//      uint32_t objectType            = mainObjectTypeID[randomType(randomEngine)];
//      uint32_t objectMaterialVariant = randomMaterialVariant[objectType](randomEngine);
//      uint32_t anim                  = randomAnimation(randomEngine);
//      frameData[0].positionData.push_back(PositionData(glm::translate(glm::mat4(), glm::vec3(pos.x, pos.y, 0.0f)) * glm::rotate(glm::mat4(), rot, glm::vec3(0.0f, 0.0f, 1.0f))));
//      frameData[0].instanceData.push_back(InstanceData(i, objectType, objectMaterialVariant, 1));
//      frameData[0].instanceDataCPU.push_back(InstanceDataCPU(anim, pos, rot, animationSpeed[anim], randomTime2NextTurn(randomEngine), randomAnimationOffset(randomEngine)));
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
//          uint32_t clothType = skeletalAssetBuffer->getTypeID(c);
//          frameData[0].instanceData.push_back(InstanceData(i, clothType, 0, 0));
//          frameData[0].instanceDataCPU.push_back(InstanceDataCPU(anim, pos, rot, animationSpeed[anim], 0.0, 0.0));
        }
      }
    }
    updateData.cameraPosition              = glm::vec3(0.0f, 0.0f, 0.0f);
    updateData.cameraGeographicCoordinates = glm::vec2(0.0f, 0.0f);
    updateData.cameraDistance              = 1.0f;
    updateData.leftMouseKeyPressed         = false;
    updateData.rightMouseKeyPressed        = false;
    updateData.xKeyPressed                 = false;

//    positionSbo->set(frameData[1].positionData);
//    instanceSbo->set(frameData[1].instanceData);

    std::vector<pumex::DrawIndexedIndirectCommand> results;
    skeletalAssetBuffer->prepareDrawIndexedIndirectCommandBuffer(1,results, resultsGeomToType);
    resultsSbo->set(results);
    resultsSbo2->set(results);
    offValuesSbo->set(std::vector<uint32_t>(1)); // FIXME

  }

  void surfaceSetup(std::shared_ptr<pumex::Surface> surface)
  {
    std::shared_ptr<pumex::Device> deviceSh = surface->device.lock();
    VkDevice vkDevice = deviceSh->device;

    myCmdBuffer[vkDevice] = std::make_shared<pumex::CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, deviceSh, surface->commandPool);

    pipelineCache->validate(deviceSh);

    skeletalAssetBuffer->validate(deviceSh, true, surface->commandPool, surface->presentationQueue);
    materialSet->validate(deviceSh, surface->commandPool, surface->presentationQueue);
    simpleRenderDescriptorSetLayout->validate(deviceSh);
    simpleRenderDescriptorPool->validate(deviceSh);
    simpleRenderPipelineLayout->validate(deviceSh);
    simpleRenderPipeline->validate(deviceSh);

    instancedRenderDescriptorSetLayout->validate(deviceSh);
    instancedRenderDescriptorPool->validate(deviceSh);
    instancedRenderPipelineLayout->validate(deviceSh);
    instancedRenderPipeline->validate(deviceSh);

    filterDescriptorSetLayout->validate(deviceSh);
    filterDescriptorPool->validate(deviceSh);
    filterPipelineLayout->validate(deviceSh);
    filterPipeline->validate(deviceSh);

    timeStampQueryPool->validate(deviceSh);

    // preparing descriptor sets
    cameraUbo->validate(deviceSh);
    positionSbo->validate(deviceSh);
    instanceSbo->validate(deviceSh);
    resultsSbo->validate(deviceSh);
    resultsSbo2->validate(deviceSh);
    offValuesSbo->validate(deviceSh);
  }


  void processInput(std::shared_ptr<pumex::Surface> surface )
  {
    auto updateTime = surface->viewer.lock()->getUpdateTime();

#if defined(CROWD_MEASURE_TIME)
    auto inputStart = pumex::HPClock::now();
#endif
    std::shared_ptr<pumex::Window>  windowSh  = surface->window.lock();

    std::vector<pumex::MouseEvent> mouseEvents = windowSh->getMouseEvents();
    glm::vec2 mouseMove = updateData.lastMousePos;
    for (const auto& m : mouseEvents)
    {
      switch (m.type)
      {
      case pumex::MouseEvent::KEY_PRESSED:
        if (m.button == pumex::MouseEvent::LEFT)
          updateData.leftMouseKeyPressed = true;
        if (m.button == pumex::MouseEvent::RIGHT)
          updateData.rightMouseKeyPressed = true;
        mouseMove.x = m.x;
        mouseMove.y = m.y;
        updateData.lastMousePos = mouseMove;
        break;
      case pumex::MouseEvent::KEY_RELEASED:
        if (m.button == pumex::MouseEvent::LEFT)
          updateData.leftMouseKeyPressed = false;
        if (m.button == pumex::MouseEvent::RIGHT)
          updateData.rightMouseKeyPressed = false;
        break;
      case pumex::MouseEvent::MOVE:
        if (updateData.leftMouseKeyPressed || updateData.rightMouseKeyPressed)
        {
          mouseMove.x = m.x;
          mouseMove.y = m.y;
        }
        break;
      }
    }
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
    if (windowSh->isKeyPressed('W'))
      updateData.cameraPosition -= forward;
    if (windowSh->isKeyPressed('S'))
      updateData.cameraPosition += forward;
    if (windowSh->isKeyPressed('A'))
      updateData.cameraPosition -= right;
    if (windowSh->isKeyPressed('D'))
      updateData.cameraPosition += right;
    if (windowSh->isKeyPressed('X'))
    {
      if (!updateData.xKeyPressed)
      {
        updateData.renderMethod = (updateData.renderMethod + 1) % 2;
        switch (updateData.renderMethod)
        {
        case 0: LOG_INFO << "Rendering using simple method ( each entity uses its own vkCmdDrawIndexed )" << std::endl; break;
        case 1: LOG_INFO << "Rendering using instanced method ( all entities use only a single vkCmdDrawIndexedIndirect )" << std::endl; break;
        }
        updateData.xKeyPressed = true;
      }
    }
    else
      updateData.xKeyPressed = false;

    glm::vec3 eye
    (
      updateData.cameraDistance * cos(updateData.cameraGeographicCoordinates.x * 3.1415f / 180.0f) * cos(updateData.cameraGeographicCoordinates.y * 3.1415f / 180.0f),
      updateData.cameraDistance * sin(updateData.cameraGeographicCoordinates.x * 3.1415f / 180.0f) * cos(updateData.cameraGeographicCoordinates.y * 3.1415f / 180.0f),
      updateData.cameraDistance * sin(updateData.cameraGeographicCoordinates.y * 3.1415f / 180.0f)
    );
    glm::mat4 viewMatrix = glm::lookAt(eye + updateData.cameraPosition, updateData.cameraPosition, glm::vec3(0, 0, 1));

    uint32_t renderWidth  = surface->swapChainSize.width;
    uint32_t renderHeight = surface->swapChainSize.height;

    frameData[writeIdx].camera.setViewMatrix(viewMatrix);
    frameData[writeIdx].camera.setObserverPosition(eye + cameraPosition);
    frameData[writeIdx].camera.setProjectionMatrix(glm::perspective(glm::radians(60.0f), (float)renderWidth / (float)renderHeight, 0.1f, 100000.0f));
    frameData[writeIdx].camera.setTimeSinceStart(surface->viewer.lock()->getApplicationDuration());
    cameraUbo->set(frameData[writeIdx].camera);

#if defined(CROWD_MEASURE_TIME)
    auto inputEnd = pumex::HPClock::now();
    inputDuration = std::chrono::duration<double, std::milli>(inputEnd - inputStart).count();
#endif
  }

  void update(double timeSinceStart, double timeSinceLastFrame)
  {
#if defined(CROWD_MEASURE_TIME)
    auto updateStart = pumex::HPClock::now();
#endif
    // parallelized version of update
    tbb::parallel_for
    (
      tbb::blocked_range<size_t>(0, frameData[readIdx].instanceData.size()),
      [=](const tbb::blocked_range<size_t>& r)
      {
        for (size_t i = r.begin(); i != r.end(); ++i)
          updateInstance
          (
            frameData[readIdx].instanceData[i], frameData[readIdx].instanceDataCPU[i], frameData[readIdx].positionData[frameData[readIdx].instanceData[i].positionIndex],
            frameData[writeIdx].instanceData[i], frameData[writeIdx].instanceDataCPU[i], frameData[writeIdx].positionData[frameData[writeIdx].instanceData[i].positionIndex],
            timeSinceStart, timeSinceLastFrame
          );

      }
    );
    // serial version of update
    //for (uint32_t i = 0; i < instanceData.size(); ++i)
    //{
    //  updateInstance
    //  (
    //    frameData[readIdx].instanceData[i], frameData[readIdx].instanceDataCPU[i], frameData[readIdx].positionData[frameData[readIdx].instanceData[i].positionIndex],
    //    frameData[writeIdx].instanceData[i], frameData[writeIdx].instanceDataCPU[i], frameData[writeIdx].positionData[frameData[writeIdx].instanceData[i].positionIndex],
    //    timeSinceStart, timeSinceLastFrame
    //  };
    // }
#if defined(CROWD_MEASURE_TIME)
    auto updateEnd = pumex::HPClock::now();
    updateDuration = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
#endif
  }

  inline void updateInstance( const InstanceData& inInstanceData, const InstanceDataCPU& inInstanceCPU, const PositionData& inPosition,
    InstanceData& outInstanceData, InstanceDataCPU& outInstanceCPU, PositionData& outPosition,
    float timeSinceStart, float timeSinceLastFrame)
  {
    // skip animation calculations for instances that are not needed
    if (inInstanceData.mainInstance == 0)
      return;
    // change rotation, animation and speed if bot requires it
    if (outInstanceCPU.time2NextTurn < 0.0f)
    {
      outInstanceCPU.rotation      = randomRotation(randomEngine);
      outInstanceCPU.time2NextTurn = randomTime2NextTurn(randomEngine);
      outInstanceCPU.animation     = randomAnimation(randomEngine);
      outInstanceCPU.speed         = animationSpeed[outInstanceCPU.animation];
    }
    else
    {
      outInstanceCPU.rotation      = inInstanceCPU.rotation;
      outInstanceCPU.time2NextTurn = inInstanceCPU.time2NextTurn - timeSinceLastFrame;
      outInstanceCPU.animation     = inInstanceCPU.animation;
      outInstanceCPU.speed         = inInstanceCPU.speed;
    }
    outInstanceCPU.animationOffset = inInstanceCPU.animationOffset;
    // calculate new position
    glm::mat4 rotationMatrix = glm::rotate(glm::mat4(), glm::radians(outInstanceCPU.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec4 direction = rotationMatrix * glm::rotate(glm::mat4(), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f))  * glm::vec4(1, 0, 0, 1);// MakeHuman models are rotated looking at Y=-1, we have to rotate it
    glm::vec2 dir2(direction.x, direction.y);
    outInstanceCPU.position = inInstanceCPU.position + dir2 * outInstanceCPU.speed * timeSinceLastFrame;

    // change direction if bot is leaving designated area
    bool isOutside[] =
    {
      outInstanceCPU.position.x < minArea.x ,
      outInstanceCPU.position.x > maxArea.x ,
      outInstanceCPU.position.y < minArea.y ,
      outInstanceCPU.position.y > maxArea.y
    };
    if (isOutside[0] || isOutside[1] || isOutside[2] || isOutside[3])
    {
      outInstanceCPU.position.x = std::max(outInstanceCPU.position.x, minArea.x);
      outInstanceCPU.position.x = std::min(outInstanceCPU.position.x, maxArea.x);
      outInstanceCPU.position.y = std::max(outInstanceCPU.position.y, minArea.y);
      outInstanceCPU.position.y = std::min(outInstanceCPU.position.y, maxArea.y);
      glm::vec4 direction = rotationMatrix * glm::rotate(glm::mat4(), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f))  * glm::vec4(1, 0, 0, 1);// MakeHuman models are rotated looking at Y=-1, we have to rotate it
      if (isOutside[0] || isOutside[1])
        direction.x *= -1.0f;
      if (isOutside[2] || isOutside[3])
        direction.y *= -1.0f;
      direction = glm::rotate(glm::mat4(), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f))  * direction;
      outInstanceCPU.rotation = glm::degrees(atan2f(direction.y, direction.x));
      rotationMatrix = glm::rotate(glm::mat4(), glm::radians(outInstanceCPU.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
      outInstanceCPU.time2NextTurn = randomTime2NextTurn(randomEngine);
    }

    outPosition.position = glm::translate(glm::mat4(), glm::vec3(outInstanceCPU.position.x, outInstanceCPU.position.y, 0.0f)) * rotationMatrix;
    outInstanceData = inInstanceData;

    // calculate bone matrices for the bots
    pumex::Animation& anim = animations[outInstanceCPU.animation];
    pumex::Skeleton&  skel = skeletons[outInstanceData.typeID];

    uint32_t numAnimChannels = anim.channels.size();
    uint32_t numSkelBones = skel.bones.size();
    SkelAnimKey saKey(outInstanceData.typeID, outInstanceCPU.animation);

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
    anim.calculateLocalTransforms(timeSinceStart + outInstanceCPU.animationOffset, localTransforms.data(), numAnimChannels);
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
      outPosition.bones[boneIndex] = globalTransforms[boneIndex] * skel.bones[boneIndex].offsetMatrix;
  }

  void recalcOffsetsAndSetData(FrameData& fData)
  {
#if defined(CROWD_MEASURE_TIME)
    auto recalcStart = pumex::HPClock::now();
#endif
    if (fData.renderMethod == 1)
    {
      std::vector<uint32_t> typeCount(skeletalAssetBuffer->getNumTypesID());
      std::fill(typeCount.begin(), typeCount.end(), 0);
      // compute how many instances of each type there is
      for (uint32_t i = 0; i<fData.instanceData.size(); ++i)
        typeCount[fData.instanceData[i].typeID]++;

      std::vector<uint32_t> offsets;
      for (uint32_t i = 0; i<resultsGeomToType.size(); ++i)
        offsets.push_back(typeCount[resultsGeomToType[i]]);

      std::vector<pumex::DrawIndexedIndirectCommand> results = resultsSbo->get();
      uint32_t offsetSum = 0;
      for (uint32_t i = 0; i<offsets.size(); ++i)
      {
        uint32_t tmp = offsetSum;
        offsetSum += offsets[i];
        offsets[i] = tmp;
        results[i].firstInstance = tmp;
      }
      resultsSbo->set(results);
      offValuesSbo->set(std::vector<uint32_t>(offsetSum));
    }
    positionSbo->set(fData.positionData);
    instanceSbo->set(fData.instanceData);
#if defined(CROWD_MEASURE_TIME)
    auto recalcEnd = pumex::HPClock::now();
    recalcDuration = std::chrono::duration<double, std::milli>(recalcEnd - recalcStart).count();
#endif

  }

  void draw( std::shared_ptr<pumex::Surface> surface)
  {
    std::shared_ptr<pumex::Device>  deviceSh = surface->device.lock();
    VkDevice                        vkDevice = deviceSh->device;

    uint32_t renderWidth = surface->swapChainSize.width;
    uint32_t renderHeight = surface->swapChainSize.height;

    cameraUbo->validate(deviceSh);
    positionSbo->validate(deviceSh);
    instanceSbo->validate(deviceSh);
    resultsSbo->validate(deviceSh);
    offValuesSbo->validate(deviceSh);

    simpleRenderDescriptorSet->validate(deviceSh);
    instancedRenderDescriptorSet->validate(deviceSh);
    filterDescriptorSet->validate(deviceSh);


#if defined(CROWD_MEASURE_TIME)
    auto drawStart = pumex::HPClock::now();
#endif
    myCmdBuffer[vkDevice]->cmdBegin();
    timeStampQueryPool->reset(deviceSh, myCmdBuffer[vkDevice], surface->swapChainImageIndex * 4, 4);

    pumex::DescriptorSetValue resultsBuffer = resultsSbo->getDescriptorSetValue(vkDevice);
    pumex::DescriptorSetValue resultsBuffer2 = resultsSbo2->getDescriptorSetValue(vkDevice);
    uint32_t drawCount = resultsSbo->get().size();

    if (frameData[readIdx].renderMethod == 1)
    {
#if defined(CROWD_MEASURE_TIME)
      timeStampQueryPool->queryTimeStamp(deviceSh, myCmdBuffer[vkDevice], surface->swapChainImageIndex * 4 + 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif
      // Add memory barrier to ensure that the indirect commands have been consumed before the compute shader updates them
      pumex::PipelineBarrier beforeBufferBarrier(VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, surface->presentationQueueFamilyIndex, surface->presentationQueueFamilyIndex, resultsBuffer.bufferInfo);
      myCmdBuffer[vkDevice]->cmdPipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, beforeBufferBarrier);

      myCmdBuffer[vkDevice]->cmdBindPipeline(filterPipeline);
      myCmdBuffer[vkDevice]->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, filterPipelineLayout, 0, filterDescriptorSet);
      myCmdBuffer[vkDevice]->cmdDispatch(frameData[readIdx].instanceData.size() / 16 + ((frameData[readIdx].instanceData.size() % 16>0) ? 1 : 0), 1, 1);

      pumex::PipelineBarrier afterBufferBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, surface->presentationQueueFamilyIndex, surface->presentationQueueFamilyIndex, resultsBuffer.bufferInfo);
      myCmdBuffer[vkDevice]->cmdPipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, afterBufferBarrier);

      VkBufferCopy copyRegion{};
      copyRegion.srcOffset = resultsBuffer.bufferInfo.offset;
      copyRegion.size = resultsBuffer.bufferInfo.range;
      copyRegion.dstOffset = resultsBuffer2.bufferInfo.offset;
      myCmdBuffer[vkDevice]->cmdCopyBuffer(resultsBuffer.bufferInfo.buffer, resultsBuffer2.bufferInfo.buffer, copyRegion);

      pumex::PipelineBarrier afterCopyBufferBarrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, surface->presentationQueueFamilyIndex, surface->presentationQueueFamilyIndex, resultsBuffer2.bufferInfo);
      myCmdBuffer[vkDevice]->cmdPipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, afterCopyBufferBarrier);

#if defined(CROWD_MEASURE_TIME)
      timeStampQueryPool->queryTimeStamp(deviceSh, myCmdBuffer[vkDevice], surface->swapChainImageIndex * 4 + 1, VK_PIPELINE_STAGE_TRANSFER_BIT);
#endif
    }

    std::vector<VkClearValue> clearValues = { pumex::makeColorClearValue(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), pumex::makeDepthStencilClearValue(1.0f, 0) };
    myCmdBuffer[vkDevice]->cmdBeginRenderPass(defaultRenderPass, surface->getCurrentFrameBuffer(), pumex::makeVkRect2D(0, 0, renderWidth, renderHeight), clearValues);
    myCmdBuffer[vkDevice]->cmdSetViewport(0, { pumex::makeViewport(0, 0, renderWidth, renderHeight, 0.0f, 1.0f) });
    myCmdBuffer[vkDevice]->cmdSetScissor(0, { pumex::makeVkRect2D(0, 0, renderWidth, renderHeight) });

#if defined(CROWD_MEASURE_TIME)
    timeStampQueryPool->queryTimeStamp(deviceSh, myCmdBuffer[vkDevice], surface->swapChainImageIndex * 4 + 2, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
#endif
    switch (frameData[readIdx].renderMethod)
    {
    case 0: // simple rendering: no compute culling, no instancing
    {
      myCmdBuffer[vkDevice]->cmdBindPipeline(simpleRenderPipeline);
      myCmdBuffer[vkDevice]->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, simpleRenderPipelineLayout, 0, simpleRenderDescriptorSet);
      skeletalAssetBuffer->cmdBindVertexIndexBuffer(deviceSh, myCmdBuffer[vkDevice], 1, 0);
      // Old method of LOD selecting - it works for normal cameras, but shadow cameras should have observerPosition defined by main camera
      //      glm::vec4 cameraPos = frameData.camera.getViewMatrixInverse() * glm::vec4(0,0,0,1);
      glm::vec4 cameraPos = frameData[readIdx].camera.getObserverPosition();
      for (uint32_t i = 0; i<frameData[readIdx].instanceData.size(); ++i)
      {
        glm::vec4 objectPos = frameData[readIdx].positionData[frameData[readIdx].instanceData[i].positionIndex].position[3];
        float distanceToCamera = glm::length(cameraPos - objectPos);
        skeletalAssetBuffer->cmdDrawObject(deviceSh, myCmdBuffer[vkDevice], 1, frameData[readIdx].instanceData[i].typeID, i, distanceToCamera);
      }
      break;
    }
    case 1: // compute culling and instanced rendering
    {
      myCmdBuffer[vkDevice]->cmdBindPipeline(instancedRenderPipeline);
      myCmdBuffer[vkDevice]->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, instancedRenderPipelineLayout, 0, instancedRenderDescriptorSet);
      skeletalAssetBuffer->cmdBindVertexIndexBuffer(deviceSh, myCmdBuffer[vkDevice], 1, 0);
      if (deviceSh->physical.lock()->features.multiDrawIndirect == 1)
        myCmdBuffer[vkDevice]->cmdDrawIndexedIndirect(resultsBuffer2.bufferInfo.buffer, resultsBuffer2.bufferInfo.offset, drawCount, sizeof(pumex::DrawIndexedIndirectCommand));
      else
      {
        for (uint32_t i = 0; i < drawCount; ++i)
          myCmdBuffer[vkDevice]->cmdDrawIndexedIndirect(resultsBuffer2.bufferInfo.buffer, resultsBuffer2.bufferInfo.offset + i * sizeof(pumex::DrawIndexedIndirectCommand), 1, sizeof(pumex::DrawIndexedIndirectCommand));
      }
      break;
    }
    }
#if defined(CROWD_MEASURE_TIME)
    timeStampQueryPool->queryTimeStamp(deviceSh, myCmdBuffer[vkDevice], surface->swapChainImageIndex * 4 + 3, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
#endif

    myCmdBuffer[vkDevice]->cmdEndRenderPass();
    myCmdBuffer[vkDevice]->cmdEnd();
    myCmdBuffer[vkDevice]->queueSubmit(surface->presentationQueue, { surface->imageAvailableSemaphore }, { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }, { surface->renderCompleteSemaphore }, VK_NULL_HANDLE);
#if defined(CROWD_MEASURE_TIME)
    auto drawEnd = pumex::HPClock::now();
    drawDuration = std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();
#endif

  }

  void finishFrame(std::shared_ptr<pumex::Viewer> viewer, std::shared_ptr<pumex::Surface> surface)
  {
#if defined(CROWD_MEASURE_TIME)
    std::shared_ptr<pumex::Device>  deviceSh = surface->device.lock();

    LOG_ERROR << "Frame time                : " << 1000.0 * viewer->getLastFrameDuration() << " ms ( FPS = " << 1.0 / viewer->getLastFrameDuration() << " )" << std::endl;
    LOG_ERROR << "Process input             : " << inputDuration << " ms" << std::endl;
    LOG_ERROR << "Update skeletons          : " << updateDuration << " ms" << std::endl;
    LOG_ERROR << "Recalculate offsets       : " << recalcDuration << " ms" << std::endl;
    LOG_ERROR << "Fill command line buffers : " << drawDuration << " ms" << std::endl;

    float timeStampPeriod = deviceSh->physical.lock()->properties.limits.timestampPeriod / 1000000.0f;
    std::vector<uint64_t> queryResults;
    // We use swapChainImageIndex to get the time measurments from previous frame - timeStampQueryPool works like circular buffer
    if (frameData[readIdx].renderMethod == 1)
    {
      queryResults = timeStampQueryPool->getResults(deviceSh, ((surface->swapChainImageIndex + 2) % 3) * 4, 4, 0);
      LOG_ERROR << "GPU LOD compute shader    : " << (queryResults[1] - queryResults[0]) * timeStampPeriod << " ms" << std::endl;
      LOG_ERROR << "GPU draw shader           : " << (queryResults[3] - queryResults[2]) * timeStampPeriod << " ms" << std::endl;
    }
    else
    {
      queryResults = timeStampQueryPool->getResults(deviceSh, ((surface->swapChainImageIndex + 2) % 3) * 4 + 2, 2, 0);
      LOG_ERROR << "GPU draw duration         : " << (queryResults[1] - queryResults[0]) * timeStampPeriod << " ms" << std::endl;
    }
    LOG_ERROR << std::endl;
#endif
    swapFrameData();
  }


  void swapFrameData()
  {
    std::swap(readIdx, writeIdx);
  }

};


int main(void)
{
  SET_LOG_INFO;
  LOG_INFO << "Crowd rendering" << std::endl;
	
  const std::vector<std::string> requestDebugLayers = { { "VK_LAYER_LUNARG_standard_validation" } };
  pumex::ViewerTraits viewerTraits{ "Crowd rendering application", true, requestDebugLayers, 100 };
  viewerTraits.debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT;// | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;

  std::shared_ptr<pumex::Viewer> viewer;
  try
  {
    viewer = std::make_shared<pumex::Viewer>(viewerTraits);

    std::vector<pumex::QueueTraits> requestQueues    = { { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, { 0.75f } } };
    std::vector<const char*> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device            = viewer->addDevice(0, requestQueues, requestDeviceExtensions);
    CHECK_LOG_THROW(!device->isValid(), "Cannot create logical device with requested parameters" );

    pumex::WindowTraits windowTraits{0, 100, 100, 640, 480, false, "Crowd rendering"};
    std::shared_ptr<pumex::Window> window = pumex::Window::createWindow(windowTraits);

    pumex::SurfaceTraits surfaceTraits{ 3, VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, VK_FORMAT_D24_UNORM_S8_UINT, VK_PRESENT_MODE_FIFO_KHR, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    surfaceTraits.definePresentationQueue(pumex::QueueTraits{ VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, { 0.75f } });

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
//    { { 0, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, 0 } };

    std::shared_ptr<pumex::RenderPass> renderPass = std::make_shared<pumex::RenderPass>(renderPassAttachments, renderPassSubpasses, renderPassDependencies);
    surfaceTraits.setDefaultRenderPass(renderPass);

    std::shared_ptr<ApplicationData> applicationData = std::make_shared<ApplicationData>(viewer);
    applicationData->defaultRenderPass = renderPass;
    applicationData->setup(glm::vec3(-25, -25, 0), glm::vec3(25, 25, 0), 200000);

    std::shared_ptr<pumex::Surface> surface = viewer->addSurface(window, device, surfaceTraits);
    applicationData->surfaceSetup(surface);

    // Making the update graph
    // The update in this example is "almost" singlethreaded. 
    // In more complicated scenarios update should be also divided into advanced update graph.
    // Consider make_edge() in update graph :
    // viewer->startUpdateGraph should point to all root nodes.
    // All leaf nodes should point to viewer->endUpdateGraph.
    tbb::flow::continue_node< tbb::flow::continue_msg > update(viewer->updateGraph, [=](tbb::flow::continue_msg)
    {
      applicationData->processInput(surface); 
      applicationData->update(pumex::inSeconds( viewer->getApplicationDuration() ), viewer->getUpdateDuration());
      applicationData->recalcOffsetsAndSetData(applicationData->frameData[applicationData->writeIdx]);
    });

    tbb::flow::make_edge(viewer->startUpdateGraph, update);
    tbb::flow::make_edge(update, viewer->endUpdateGraph);

    // Making the render graph.
    // This one is also "single threaded" ( look at the make_edge() calls ), but presents a method of connecting graph nodes.
    // Consider make_edge() in render graph :
    // viewer->startRenderGraph should point to all root nodes.
    // All leaf nodes should point to viewer->endRenderGraph.
    tbb::flow::continue_node< tbb::flow::continue_msg > startSurfaceFrame(viewer->renderGraph, [=](tbb::flow::continue_msg) { surface->beginFrame(); });
    tbb::flow::continue_node< tbb::flow::continue_msg > drawSurfaceFrame(viewer->renderGraph, [=](tbb::flow::continue_msg) { applicationData->draw(surface); });
    tbb::flow::continue_node< tbb::flow::continue_msg > endSurfaceFrame(viewer->renderGraph, [=](tbb::flow::continue_msg) { surface->endFrame(); });
    tbb::flow::continue_node< tbb::flow::continue_msg > endWholeFrame(viewer->renderGraph, [=](tbb::flow::continue_msg) { applicationData->finishFrame(viewer, surface); });

    tbb::flow::make_edge(viewer->startRenderGraph, startSurfaceFrame);
    tbb::flow::make_edge(startSurfaceFrame, drawSurfaceFrame);
    tbb::flow::make_edge(drawSurfaceFrame, endSurfaceFrame);
    tbb::flow::make_edge(endSurfaceFrame, endWholeFrame);
    tbb::flow::make_edge(endWholeFrame, viewer->endRenderGraph);

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
// glslangvalidator -H instanced_animation.vert -o instanced_animation.vert.spv >>instanced_animation.vert.txt
// glslangvalidator -H instanced_animation.frag -o instanced_animation.frag.spv >>instanced_animation.frag.txt
