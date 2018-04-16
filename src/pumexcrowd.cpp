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

// global variables storing model file names etc
std::vector<std::string> animationFileNames
{
  "people/wmale1_bbox.dae",
  "people/wmale1_walk.dae",
  "people/wmale1_walk_easy.dae",
  "people/wmale1_walk_big_steps.dae",
  "people/wmale1_run.dae"
};

std::vector<float> animationSpeed =  // in meters per sec
{
  0.0f,
  1.0f,
  0.8f,
  1.2f,
  2.0f
};

std::vector<std::pair<std::string, bool>> skeletalNames
{
  { "wmale1", true },
  { "wmale2", true },
  { "wmale3", true },
  { "wmale1_cloth1", false },
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
  pumex::AssetLodDefinition(0.0f, 8.0f),   pumex::AssetLodDefinition(8.0f, 16.0f), pumex::AssetLodDefinition(16.0f, 100.0f),
  pumex::AssetLodDefinition(0.0f, 8.0f),   pumex::AssetLodDefinition(8.0f, 16.0f), pumex::AssetLodDefinition(16.0f, 100.0f),
  pumex::AssetLodDefinition(0.0f, 8.0f),   pumex::AssetLodDefinition(8.0f, 16.0f), pumex::AssetLodDefinition(16.0f, 100.0f),
  pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f),  pumex::AssetLodDefinition(0.0f, 0.0f),
  pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f),  pumex::AssetLodDefinition(0.0f, 0.0f),
  pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f),  pumex::AssetLodDefinition(0.0f, 0.0f),
  pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f),  pumex::AssetLodDefinition(0.0f, 0.0f),
  pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f),  pumex::AssetLodDefinition(0.0f, 0.0f),
  pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f),  pumex::AssetLodDefinition(0.0f, 0.0f),
  pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f),  pumex::AssetLodDefinition(0.0f, 0.0f),
  pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f),  pumex::AssetLodDefinition(0.0f, 0.0f),
  pumex::AssetLodDefinition(0.0f, 100.0f), pumex::AssetLodDefinition(0.0f, 0.0f),  pumex::AssetLodDefinition(0.0f, 0.0f)
};

std::multimap<std::string, std::vector<std::vector<std::string>>> materialVariants =
{
  { "wmale1", { { "body_mat", "people/young_lightskinned_male_diffuse_1.dds" } } },
  { "wmale1", { { "body_mat", "people/young_lightskinned_male_diffuse.dds" } } },
  { "wmale2", { { "body_mat", "people/young_lightskinned_male_diffuse3_1.dds" } } },
  { "wmale2", { { "body_mat", "people/dragon_female_white.dds" } } },
  { "wmale3", { { "body_mat", "people/middleage_lightskinned_male_diffuse_1.dds" } } },
  { "wmale3", { { "body_mat", "people/ork_texture.dds" } } }
};

std::multimap<std::string, std::vector<std::string>> clothVariants =
{
  { "wmale1", {} },
  { "wmale1", { "wmale1_cloth1" } },
  { "wmale1", { "wmale1_cloth2" } },
  { "wmale1", { "wmale1_cloth3" } },
  { "wmale2", {} },
  { "wmale2", { "wmale2_cloth1" } },
  { "wmale2", { "wmale2_cloth2" } },
  { "wmale2", { "wmale2_cloth3" } },
  { "wmale3", {} },
  { "wmale3", { "wmale3_cloth1" } },
  { "wmale3", { "wmale3_cloth2" } },
  { "wmale3", { "wmale3_cloth3" } }
};


struct CrowdApplicationData
{
  UpdateData                                             updateData;
  std::array<RenderData, 3>                              renderData;

  glm::vec3                                              minArea;
  glm::vec3                                              maxArea;
  std::vector<pumex::Animation>                          animations;
  std::vector<pumex::Skeleton>                           skeletons;

  std::map<SkelAnimKey, std::vector<uint32_t>>           skelAnimBoneMapping;

  std::default_random_engine                             randomEngine;
  std::exponential_distribution<float>                   randomTime2NextTurn;
  std::uniform_real_distribution<float>                  randomRotation;
  std::uniform_int_distribution<uint32_t>                randomAnimation;

  std::shared_ptr<pumex::AssetBuffer>                    skeletalAssetBuffer;
  std::shared_ptr<pumex::AssetBufferInstancedResults>    instancedResults;

  std::shared_ptr<pumex::UniformBufferPerSurface<pumex::Camera>> cameraUbo;
  std::shared_ptr<pumex::UniformBufferPerSurface<pumex::Camera>> textCameraUbo;
  std::shared_ptr<pumex::StorageBuffer<PositionData>>            positionSbo;
  std::shared_ptr<pumex::StorageBuffer<InstanceData>>            instanceSbo;

  //std::shared_ptr<pumex::QueryPool>                      timeStampQueryPool;

  pumex::HPClock::time_point                             lastFrameStart;
  bool                                                   measureTime = true;
  std::mutex                                             measureMutex;
  std::unordered_map<uint32_t, double>                   times;

  std::unordered_map<uint32_t, glm::mat4>                slaveViewMatrix;


  CrowdApplicationData()
	  : randomTime2NextTurn{ 0.25 }, randomRotation{ -glm::pi<float>(), glm::pi<float>() }
  {
  }

  void setup(const glm::vec3& minAreaParam, const glm::vec3& maxAreaParam, float objectDensity, const std::vector<uint32_t>& mainObjectTypeID, const std::vector<uint32_t>& materialVariantCount, std::shared_ptr<pumex::AssetBuffer> assetBuffer, std::shared_ptr<pumex::AssetBufferInstancedResults> iResults, std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator, const std::vector<pumex::Animation>& anims, const std::vector<pumex::Skeleton> skels)
  {
    minArea             = minAreaParam;
    maxArea             = maxAreaParam;
    skeletalAssetBuffer = assetBuffer;
    instancedResults    = iResults;
    animations          = anims;
    skeletons           = skels;

    randomAnimation = std::uniform_int_distribution<uint32_t>(1, animations.size() - 1);

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

    cameraUbo     = std::make_shared<pumex::UniformBufferPerSurface<pumex::Camera>>(buffersAllocator);
    textCameraUbo = std::make_shared<pumex::UniformBufferPerSurface<pumex::Camera>>(buffersAllocator);
    positionSbo   = std::make_shared<pumex::StorageBuffer<PositionData>>(buffersAllocator);
    instanceSbo   = std::make_shared<pumex::StorageBuffer<InstanceData>>(buffersAllocator);

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

  void processInput( std::shared_ptr<pumex::Surface> surface )
  {
    std::shared_ptr<pumex::Window> window = surface->window.lock();
    std::shared_ptr<pumex::Viewer> viewer = surface->viewer.lock();


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

    uint32_t updateIndex = viewer->getUpdateIndex();
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

    //if (measureTime != updateData.measureTime)
    //{
    //  for (auto& cb : myCmdBuffer)
    //    cb.second->setDirty(UINT32_MAX);
    //  measureTime = updateData.measureTime;
    //}

    uData.cameraGeographicCoordinates = updateData.cameraGeographicCoordinates;
    uData.cameraDistance              = updateData.cameraDistance;
    uData.cameraPosition              = updateData.cameraPosition;
  }

  void update(std::shared_ptr<pumex::Viewer> viewer, double timeSinceStart, double updateStep)
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
    uint32_t updateIndex = viewer->getUpdateIndex();

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
    std::shared_ptr<pumex::Viewer> viewer = surface->viewer.lock();
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

  void prepareBuffersForRendering( std::shared_ptr<pumex::Viewer> viewer )
  {
    pumex::HPClock::time_point thisFrameStart = pumex::HPClock::now();
    double fpsValue = 1.0 / pumex::inSeconds(thisFrameStart - lastFrameStart);
    lastFrameStart = thisFrameStart;

    uint32_t renderIndex = viewer->getRenderIndex();
    const RenderData& rData = renderData[renderIndex];

    float deltaTime  = pumex::inSeconds(viewer->getRenderTimeDelta());
    float renderTime = pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()) + deltaTime;

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

//    std::wstringstream stream;
//    stream << "FPS : " << std::fixed << std::setprecision(1) << fpsValue;
//    textDefault->setText(viewer.lock()->getSurface(0), 0, glm::vec2(30, 28), glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), stream.str());
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

    std::vector<const char*> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device            = viewer->addDevice(0, requestDeviceExtensions);

    std::vector<std::shared_ptr<pumex::Window>> windows;
    for (const auto& wt : windowTraits)
      windows.push_back(pumex::Window::createWindow(wt));

    pumex::SurfaceTraits surfaceTraits{ 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, VK_PRESENT_MODE_MAILBOX_KHR, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    std::vector<std::shared_ptr<pumex::Surface>> surfaces;
    for (auto win : windows)
      surfaces.push_back(viewer->addSurface(win, device, surfaceTraits));

    // allocate 24 MB for frame buffers
    std::shared_ptr<pumex::DeviceMemoryAllocator> frameBufferAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 24 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);

    std::vector<pumex::QueueTraits> queueTraits{ { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, 0.75f } };

    std::shared_ptr<pumex::RenderWorkflow> workflow = std::make_shared<pumex::RenderWorkflow>("crowd_workflow", frameBufferAllocator, queueTraits);
      workflow->addResourceType(std::make_shared<pumex::RenderWorkflowResourceType>("depth_samples",   false, VK_FORMAT_D32_SFLOAT,        VK_SAMPLE_COUNT_1_BIT, pumex::atDepth,   pumex::AttachmentSize{ pumex::AttachmentSize::SurfaceDependent, glm::vec2(1.0f,1.0f) }));
      workflow->addResourceType(std::make_shared<pumex::RenderWorkflowResourceType>("surface",         true,  VK_FORMAT_B8G8R8A8_UNORM,    VK_SAMPLE_COUNT_1_BIT, pumex::atSurface, pumex::AttachmentSize{ pumex::AttachmentSize::SurfaceDependent, glm::vec2(1.0f,1.0f) }));
      workflow->addResourceType(std::make_shared<pumex::RenderWorkflowResourceType>("compute_results", true));

    workflow->addRenderOperation(std::make_shared<pumex::RenderOperation>("crowd_compute", pumex::RenderOperation::Compute));
      workflow->addBufferOutput( "crowd_compute", "compute_results", "indirect_commands", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT );
      workflow->addBufferOutput( "crowd_compute", "compute_results", "offset_values",     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT );

    workflow->addRenderOperation(std::make_shared<pumex::RenderOperation>("rendering", pumex::RenderOperation::Graphics));
      workflow->addBufferInput          ( "rendering", "compute_results", "indirect_commands", VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT );
      workflow->addBufferInput          ( "rendering", "compute_results", "offset_values",     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT );
      workflow->addAttachmentDepthOutput( "rendering", "depth_samples",   "depth",             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, pumex::loadOpClear(glm::vec2(1.0f, 0.0f)));
      workflow->addAttachmentOutput     ( "rendering", "surface",         "color",             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,         pumex::loadOpClear(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)));

    // alocate 12 MB for uniform and storage buffers
    auto buffersAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 12 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 64 MB for vertex and index buffers
    auto verticesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 64 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 80 MB memory for 24 compressed textures and for font textures
    auto texturesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 80 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);

    std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 },{ pumex::VertexSemantic::Normal, 3 },{ pumex::VertexSemantic::TexCoord, 3 },{ pumex::VertexSemantic::BoneWeight, 4 },{ pumex::VertexSemantic::BoneIndex, 4 } };
    std::vector<pumex::AssetBufferVertexSemantics> assetSemantics = { { MAIN_RENDER_MASK, vertexSemantic } };

    auto skeletalAssetBuffer = std::make_shared<pumex::AssetBuffer>(assetSemantics, buffersAllocator, verticesAllocator);
    auto instancedResults = std::make_shared<pumex::AssetBufferInstancedResults>(assetSemantics, skeletalAssetBuffer, buffersAllocator);
    workflow->associateResource("indirect_commands", instancedResults->getResults(MAIN_RENDER_MASK));
    workflow->associateResource("offset_values",     instancedResults->getOffsetValues(MAIN_RENDER_MASK));

    std::shared_ptr<pumex::TextureRegistryTextureArray>    textureRegistry  = std::make_shared<pumex::TextureRegistryTextureArray>();
    textureRegistry->setTargetTexture(0, std::make_shared<pumex::Texture>(gli::texture(gli::target::TARGET_2D_ARRAY, gli::format::FORMAT_RGBA_DXT1_UNORM_BLOCK8, gli::texture::extent_type(2048, 2048, 1), 24, 1, 12), pumex::SamplerTraits(), texturesAllocator));
    std::vector<pumex::TextureSemantic>                    textureSemantic  = { { pumex::TextureSemantic::Diffuse, 0 } };
    std::shared_ptr<pumex::MaterialRegistry<MaterialData>> materialRegistry = std::make_shared<pumex::MaterialRegistry<MaterialData>>(buffersAllocator);
    std::shared_ptr<pumex::MaterialSet>                    materialSet      = std::make_shared<pumex::MaterialSet>(viewer, materialRegistry, textureRegistry, buffersAllocator, textureSemantic);

    pumex::AssetLoaderAssimp      loader;
    std::vector<pumex::Animation> animations;

    // We assume that animations use the same skeleton as skeletal models
    for (uint32_t i = 0; i < animationFileNames.size(); ++i)
    {
      std::string fullAssetFileName = viewer->getFullFilePath(animationFileNames[i]);
      if (fullAssetFileName.empty())
      {
        LOG_WARNING << "Cannot find asset : " << animationFileNames[i] << std::endl;
        continue;
      }
      std::shared_ptr<pumex::Asset> asset(loader.load(fullAssetFileName, true));
      if (asset.get() == nullptr)
      {
        LOG_WARNING << "Cannot load asset : " << fullAssetFileName << std::endl;
        continue;
      }
      animations.push_back(asset->animations[0]);
    }

    std::vector<pumex::Skeleton>  skeletons;
    std::vector<uint32_t>         mainObjectTypeID;
    std::vector<uint32_t>         accessoryObjectTypeID;
    skeletons.push_back(pumex::Skeleton()); // empty skeleton for null type
    for (uint32_t i = 0; i < skeletalNames.size(); ++i)
    {
      uint32_t typeID = 0;
      for (uint32_t j = 0; j<3; ++j)
      {
        if (skeletalModels[3 * i + j].empty())
          continue;
        std::string fullAssetFileName = viewer->getFullFilePath(skeletalModels[3 * i + j]);
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

    std::shared_ptr<CrowdApplicationData> applicationData = std::make_shared<CrowdApplicationData>();
    applicationData->setup(glm::vec3(-25, -25, 0), glm::vec3(25, 25, 0), 200000, mainObjectTypeID, materialVariantCount, skeletalAssetBuffer, instancedResults, buffersAllocator, animations, skeletons);

    // build a compute tree

    auto pipelineCache = std::make_shared<pumex::PipelineCache>();

    auto computeRoot = std::make_shared<pumex::Group>();
    computeRoot->setName("computeRoot");
    workflow->setSceneNode("crowd_compute", computeRoot);

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

    // building compute pipeline layout
    auto filterDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(filterLayoutBindings);
    auto filterDescriptorPool = std::make_shared<pumex::DescriptorPool>(3 * MAX_SURFACES, filterLayoutBindings);
    auto filterPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    filterPipelineLayout->descriptorSetLayouts.push_back(filterDescriptorSetLayout);
    auto filterPipeline = std::make_shared<pumex::ComputePipeline>(pipelineCache, filterPipelineLayout);
    filterPipeline->shaderStage = { VK_SHADER_STAGE_COMPUTE_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/crowd_filter_instances.comp.spv")), "main" };
    computeRoot->addChild(filterPipeline);

    // FIXME : instance count
    uint32_t instanceCount = applicationData->updateData.people.size() + applicationData->updateData.clothes.size();
    auto dispatchNode = std::make_shared<pumex::DispatchNode>(instanceCount / 16 + ((instanceCount % 16 > 0) ? 1 : 0), 1, 1);
    dispatchNode->setName("dispatchNode");
    filterPipeline->addChild(dispatchNode);
  
    auto filterDescriptorSet = std::make_shared<pumex::DescriptorSet>(filterDescriptorSetLayout, filterDescriptorPool);
    filterDescriptorSet->setDescriptor(0, applicationData->cameraUbo);
    filterDescriptorSet->setDescriptor(1, applicationData->positionSbo);
    filterDescriptorSet->setDescriptor(2, applicationData->instanceSbo);
    filterDescriptorSet->setDescriptor(3, skeletalAssetBuffer->getTypeBuffer(MAIN_RENDER_MASK));
    filterDescriptorSet->setDescriptor(4, skeletalAssetBuffer->getLodBuffer(MAIN_RENDER_MASK));
    filterDescriptorSet->setDescriptor(5, instancedResults->getResults(MAIN_RENDER_MASK));
    filterDescriptorSet->setDescriptor(6, instancedResults->getOffsetValues(MAIN_RENDER_MASK));
    dispatchNode->setDescriptorSet(0, filterDescriptorSet);

    //    timeStampQueryPool = std::make_shared<pumex::QueryPool>(VK_QUERY_TYPE_TIMESTAMP,4 * MAX_SURFACES);

    // build a render tree

    auto renderingRoot = std::make_shared<pumex::Group>();
    renderingRoot->setName("renderingRoot");
    workflow->setSceneNode("rendering", renderingRoot);

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
    // building rendering pipeline layout
    auto instancedRenderDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(instancedRenderLayoutBindings);
    auto instancedRenderDescriptorPool      = std::make_shared<pumex::DescriptorPool>(3 * MAX_SURFACES, instancedRenderLayoutBindings);
    auto instancedRenderPipelineLayout      = std::make_shared<pumex::PipelineLayout>();
    instancedRenderPipelineLayout->descriptorSetLayouts.push_back(instancedRenderDescriptorSetLayout);
    auto instancedRenderPipeline            = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, instancedRenderPipelineLayout);
    instancedRenderPipeline->shaderStages   =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/crowd_instanced_animation.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/crowd_instanced_animation.frag.spv")), "main" }
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

    renderingRoot->addChild(instancedRenderPipeline);

    auto assetBufferNode = std::make_shared<pumex::AssetBufferNode>(skeletalAssetBuffer, materialSet, MAIN_RENDER_MASK, 0);
    assetBufferNode->setName("assetBufferNode");
    instancedRenderPipeline->addChild(assetBufferNode);

    auto assetBufferDrawIndirect = std::make_shared<pumex::AssetBufferIndirectDrawObjects>( instancedResults );
    assetBufferDrawIndirect->setName("assetBufferDrawIndirect");
    assetBufferNode->addChild(assetBufferDrawIndirect);

    auto instancedRenderDescriptorSet = std::make_shared<pumex::DescriptorSet>(instancedRenderDescriptorSetLayout, instancedRenderDescriptorPool);
    instancedRenderDescriptorSet->setDescriptor(0, applicationData->cameraUbo);
    instancedRenderDescriptorSet->setDescriptor(1, applicationData->positionSbo);
    instancedRenderDescriptorSet->setDescriptor(2, applicationData->instanceSbo);
    instancedRenderDescriptorSet->setDescriptor(3, instancedResults->getOffsetValues(MAIN_RENDER_MASK));
    instancedRenderDescriptorSet->setDescriptor(4, materialSet->typeDefinitionSbo);
    instancedRenderDescriptorSet->setDescriptor(5, materialSet->materialVariantSbo);
    instancedRenderDescriptorSet->setDescriptor(6, materialRegistry->materialDefinitionSbo);
    instancedRenderDescriptorSet->setDescriptor(7, textureRegistry->getTargetTexture(0), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    assetBufferDrawIndirect->setDescriptorSet(0, instancedRenderDescriptorSet);

    // build text render pipeline
    std::string fullFontFileName = viewer->getFullFilePath("fonts/DejaVuSans.ttf");
    auto fontDefault             = std::make_shared<pumex::Font>(fullFontFileName, glm::uvec2(1024, 1024), 24, texturesAllocator, buffersAllocator);
    auto fontSmall               = std::make_shared<pumex::Font>(fullFontFileName, glm::uvec2(512, 512),   16, texturesAllocator, buffersAllocator);

    auto textDefault = std::make_shared<pumex::Text>(fontDefault, buffersAllocator);
    auto textSmall   = std::make_shared<pumex::Text>(fontSmall, buffersAllocator);

    // building text rendering pipeline layout
    std::vector<pumex::DescriptorSetLayoutBinding> textLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_GEOMETRY_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    auto textDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(textLayoutBindings);
    auto textDescriptorPool      = std::make_shared<pumex::DescriptorPool>(6 * MAX_SURFACES, textLayoutBindings);
    auto textPipelineLayout      = std::make_shared<pumex::PipelineLayout>();
    textPipelineLayout->descriptorSetLayouts.push_back(textDescriptorSetLayout);
    auto textPipeline            = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, textPipelineLayout);
    textPipeline->vertexInput    =
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
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/text_draw.vert.spv")), "main" },
      { VK_SHADER_STAGE_GEOMETRY_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/text_draw.geom.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/text_draw.frag.spv")), "main" }
    };
    textPipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    renderingRoot->addChild(textPipeline);

    textPipeline->addChild(textDefault);
    textPipeline->addChild(textSmall);

    auto textDescriptorSet = std::make_shared<pumex::DescriptorSet>(textDescriptorSetLayout, textDescriptorPool);
    textDescriptorSet->setDescriptor(0, applicationData->textCameraUbo);
    textDescriptorSet->setDescriptor(1, fontDefault->fontTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    textDefault->setDescriptorSet(0, textDescriptorSet);

    auto textDescriptorSetSmall = std::make_shared<pumex::DescriptorSet>(textDescriptorSetLayout, textDescriptorPool);
    textDescriptorSetSmall->setDescriptor(0, applicationData->textCameraUbo);
    textDescriptorSetSmall->setDescriptor(1, fontSmall->fontTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    textSmall->setDescriptorSet(0, textDescriptorSetSmall);

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

    // connecting workflow to all surfaces
    std::shared_ptr<pumex::SingleQueueWorkflowCompiler> workflowCompiler = std::make_shared<pumex::SingleQueueWorkflowCompiler>();
    for (auto surf : surfaces)
      surf->setRenderWorkflow(workflow, workflowCompiler);

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
      applicationData->update(viewer, pumex::inSeconds( viewer->getUpdateTime() - viewer->getApplicationStartTime() ), pumex::inSeconds(viewer->getUpdateDuration()));
      applicationData->setTime(1020, updateBeginTime);
    });

    tbb::flow::make_edge(viewer->startUpdateGraph, update);
    tbb::flow::make_edge(update, viewer->endUpdateGraph);

    // set render callbacks to application data
    viewer->setEventRenderStart(std::bind(&CrowdApplicationData::prepareBuffersForRendering, applicationData, std::placeholders::_1));
    for (auto surf : surfaces)
      surf->setEventSurfaceRenderStart(std::bind(&CrowdApplicationData::prepareCameraForRendering, applicationData, std::placeholders::_1));

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
