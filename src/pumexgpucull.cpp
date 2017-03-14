#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gli/gli.hpp>
#include <pumex/Pumex.h>
#include <pumex/AssetLoaderAssimp.h>
#include <pumex/utils/Shapes.h>

// This demo shows how to render multiple different objects using a minimal number of vkCmdDrawIndexedIndirect commands. 
// Rendering consists of following parts :
// 1. Positions and parameters of all objects are sent to compute shader. Compute shader ( a filter ) culls invisible objects using 
//    camera parameters, object position and object bounding box. For visible objects the appropriate level of detail is chosen. 
//    Results are stored in a buffer.
// 2. Above mentioned buffer is used during rendering to choose appropriate object parameters ( position, bone matrices, object specific parameters, material ids, etc )
// 
// Demo presents possibility to render both static and dynamic objects :
// - static objects consist mainly of trees, so animation of waving in the wind was added ( amplitude of waving was set to 0 for buildings :) ).
// - in this demo all static objects are sent at once ( that's why compute shader takes so much time - compare it to 500 people rendered in crowd demo ). 
//   In real application CPU would only sent objects that are visible to a user. Such objects would be stored in some form of quad tree
// - dynamic objects present the possibility to animate object parts of an object ( wheels, propellers ) 
// - static and dynamic object use different set of rendering parameters : compare StaticInstanceData and DynamicInstanceData structures
//
// pumexgpucull demo is a copy of similar demo that I created for OpenSceneGraph engine 3 years ago ( osggpucull example ), so you may
// compare Vulkan and OpenGL performance ( I didn't use compute shaders in OpenGL demo, but performance of rendering is comparable ).

// all time measurments may be turned off 
#define GPU_CULL_MEASURE_TIME 1

// struct holding the whole information required to render a single static object
struct StaticInstanceData
{
  StaticInstanceData(const glm::mat4& p, uint32_t t, uint32_t m, float b, float wa, float wf, float wo)
    : position{ p }, typeID{ t }, materialVariant{ m }, brightness{ b }, wavingAmplitude{ wa }, wavingFrequency{ wf }, wavingOffset{ wo }
  {
  }
  glm::mat4 position;
  uint32_t  typeID;
  uint32_t  materialVariant;
  float     brightness;
  float     wavingAmplitude; 
  float     wavingFrequency;
  float     wavingOffset;
  uint32_t  std430pad0;
  uint32_t  std430pad1;
};

const uint32_t MAX_BONES = 9;

// struct holding the whole information required to render a single dynamic object
struct DynamicInstanceData
{
  DynamicInstanceData(const glm::mat4& p, uint32_t t, uint32_t m, float b)
    : position{ p }, typeID{ t }, materialVariant{ m }, brightness{ b }
  {
  }
  glm::mat4 position;
  glm::mat4 bones[MAX_BONES];
  uint32_t  typeID;
  uint32_t  materialVariant;
  float     brightness;
  uint32_t  std430pad0;
};

// Very simple dynamic object state that is not sent to GPU.
struct DynamicInstanceDataCPU
{
  DynamicInstanceDataCPU(const glm::vec3& p, float r, float s, float tnt)
    : position{ p }, rotation{ r }, speed{ s }, time2NextTurn{ tnt }
  {
  }
  glm::vec3 position;
  float rotation;
  float speed;
  float time2NextTurn;
};

// struct that holds information about material used by specific object type. Demo does not use textures ( in contrast to crowd example )
struct MaterialGpuCull
{
  glm::vec4 ambient;
  glm::vec4 diffuse;
  glm::vec4 specular;
  float     shininess;
  uint32_t  std430pad0;
  uint32_t  std430pad1;
  uint32_t  std430pad2;

  // two functions that define material parameters according to data from an asset's material 
  void registerProperties(const pumex::Material& material)
  {
    ambient   = material.getProperty("$clr.ambient", glm::vec4(0, 0, 0, 0));
    diffuse   = material.getProperty("$clr.diffuse", glm::vec4(1, 1, 1, 1));
    specular  = material.getProperty("$clr.specular", glm::vec4(0, 0, 0, 0));
    shininess = material.getProperty("$mat.shininess", glm::vec4(0, 0, 0, 0)).r;
  }
  // we don't use textures in that example
  void registerTextures(const std::map<pumex::TextureSemantic::Type, uint32_t>& textureIndices)
  {
  }
};

// a set of methods showing how to procedurally build an object using Skeleton, Geometry, Material and Asset classes :
pumex::Asset* createGround( float staticAreaSize, const glm::vec4& groundColor )
{
  pumex::Asset* result = new pumex::Asset;
  std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };

  pumex::Geometry ground;
    ground.name = "ground";
    ground.semantic = vertexSemantic;
    ground.materialIndex = 0;
    pumex::addQuad(ground, glm::vec3(-0.5f*staticAreaSize, -0.5f*staticAreaSize, 0.0f), glm::vec3(staticAreaSize, 0.0, 0.0), glm::vec3(0.0, staticAreaSize, 0.0));
  result->geometries.push_back(ground);
  pumex::Material groundMaterial;
    groundMaterial.properties["$clr.ambient"] = 0.5f * groundColor;
    groundMaterial.properties["$clr.diffuse"] = 0.5f * groundColor;
    groundMaterial.properties["$clr.specular"] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    groundMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(groundMaterial);

  pumex::Skeleton::Bone bone;
  result->skeleton.bones.emplace_back(bone);
  result->skeleton.boneNames.push_back("root");
  result->skeleton.invBoneNames.insert({ "root", 0 });

  return result;
}

pumex::Asset* createConiferTree(float detailRatio, const glm::vec4& leafColor, const glm::vec4& trunkColor)
{
  pumex::Asset* result = new pumex::Asset;
  std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };

  pumex::Geometry trunk;
    trunk.name          = "trunk";
    trunk.semantic      = vertexSemantic;
    trunk.materialIndex = 0;
    pumex::addCylinder(trunk, glm::vec3(0.0, 0.0, 1.0), 0.25, 2.0, detailRatio * 40, true, true, false);
  result->geometries.push_back(trunk);
  pumex::Material trunkMaterial;
    trunkMaterial.properties["$clr.ambient"]   = 0.1f * trunkColor;
    trunkMaterial.properties["$clr.diffuse"]   = 0.9f * trunkColor;
    trunkMaterial.properties["$clr.specular"]  = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    trunkMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(trunkMaterial);

  pumex::Geometry leaf;
    leaf.name          = "leaf";
    leaf.semantic      = vertexSemantic;
    leaf.materialIndex = 1;
    pumex::addCone(leaf, glm::vec3(0.0, 0.0, 2.0), 2.0, 8.0, detailRatio * 40, detailRatio * 10, true);
  result->geometries.push_back(leaf);
  pumex::Material leafMaterial;
    leafMaterial.properties["$clr.ambient"]   = 0.1f * leafColor;
    leafMaterial.properties["$clr.diffuse"]   = 0.9f * leafColor;
    leafMaterial.properties["$clr.specular"]  = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    leafMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(trunkMaterial);

  pumex::Skeleton::Bone bone;
  result->skeleton.bones.emplace_back(bone);
  result->skeleton.boneNames.push_back("root");
  result->skeleton.invBoneNames.insert({ "root", 0 });

  return result;
}

pumex::Asset* createDecidousTree(float detailRatio, const glm::vec4& leafColor, const glm::vec4& trunkColor)
{
  pumex::Asset* result = new pumex::Asset;
  std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };

  pumex::Geometry trunk;
    trunk.name          = "trunk";
    trunk.semantic      = vertexSemantic;
    trunk.materialIndex = 0;
    pumex::addCylinder(trunk, glm::vec3(0.0f, 0.0f, 1.0f), 0.4f, 2.0f, detailRatio * 40, true, true, false);
  result->geometries.push_back(trunk);
  pumex::Material trunkMaterial;
    trunkMaterial.properties["$clr.ambient"]   = 0.1f * trunkColor;
    trunkMaterial.properties["$clr.diffuse"]   = 0.9f * trunkColor;
    trunkMaterial.properties["$clr.specular"]  = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    trunkMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(trunkMaterial);

  pumex::Geometry leaf;
    leaf.name          = "leaf";
    leaf.semantic      = vertexSemantic;
    leaf.materialIndex = 1;
    pumex::addCapsule(leaf, glm::vec3(0.0, 0.0, 7.4), 3.0, 5.0, detailRatio * 40, detailRatio * 20, true, true, true);
    result->geometries.push_back(leaf);
  pumex::Material leafMaterial;
    leafMaterial.properties["$clr.ambient"]   = 0.1f * leafColor;
    leafMaterial.properties["$clr.diffuse"]   = 0.9f * leafColor;
    leafMaterial.properties["$clr.specular"]  = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    leafMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(trunkMaterial);

  pumex::Skeleton::Bone bone;
  result->skeleton.bones.emplace_back(bone);
  result->skeleton.boneNames.push_back("root");
  result->skeleton.invBoneNames.insert({ "root", 0 });

  return result;
}

pumex::Asset* createSimpleHouse(float detailRatio, const glm::vec4& buildingColor, const glm::vec4& chimneyColor)
{
  pumex::Asset* result = new pumex::Asset;
  std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };

  pumex::Geometry building;
  building.name = "building";
  building.semantic = vertexSemantic;
  building.materialIndex = 0;
  pumex::addBox(building, glm::vec3(-7.5f, -4.5f, 0.0f), glm::vec3(7.5f, 4.5f, 16.0f));
  result->geometries.push_back(building);
  pumex::Material buildingMaterial;
  buildingMaterial.properties["$clr.ambient"] = 0.1f * buildingColor;
  buildingMaterial.properties["$clr.diffuse"] = 0.9f * buildingColor;
  buildingMaterial.properties["$clr.specular"] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  buildingMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(buildingMaterial);

  pumex::Geometry chimney;
  chimney.name = "chimneys";
  chimney.semantic = vertexSemantic;
  chimney.materialIndex = 1;
  pumex::addCylinder(chimney, glm::vec3(-6.0f, 3.0f, 16.75f), 0.1f, 1.5f, detailRatio * 40, true, false, true);
  pumex::addCylinder(chimney, glm::vec3(-5.5f, 3.0f, 16.5f),  0.1f, 1.0f, detailRatio * 40, true, false, true);
  pumex::addCylinder(chimney, glm::vec3(-5.0f, 3.0f, 16.25f), 0.1f, 0.5f, detailRatio * 40, true, false, true);
  result->geometries.push_back(chimney);
  pumex::Material chimneyMaterial;
  chimneyMaterial.properties["$clr.ambient"] = 0.1f * chimneyColor;
  chimneyMaterial.properties["$clr.diffuse"] = 0.9f * chimneyColor;
  chimneyMaterial.properties["$clr.specular"] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  chimneyMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(chimneyMaterial);

  pumex::Skeleton::Bone bone;
  result->skeleton.bones.emplace_back(bone);
  result->skeleton.boneNames.push_back("root");
  result->skeleton.invBoneNames.insert({ "root", 0 });

  return result;
}

pumex::Asset* createPropeller(const std::string& boneName, float detailRatio, int propNum, float propRadius, const glm::vec4& color)
{
  pumex::Asset* result = new pumex::Asset;
  std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };
  uint32_t oneVertexSize = pumex::calcVertexSize(vertexSemantic);

  pumex::Material propellerMaterial;
  propellerMaterial.properties["$clr.ambient"]   = 0.1f * color;
  propellerMaterial.properties["$clr.diffuse"]   = 0.9f * color;
  propellerMaterial.properties["$clr.specular"]  = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  propellerMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(propellerMaterial);

  pumex::Geometry propeller;
  propeller.name          = "propeller";
  propeller.semantic      = vertexSemantic;
  propeller.materialIndex = 0;
  // add center
  pumex::addCone(propeller, glm::vec3(0.0, 0.0, 0.0), 0.1 * propRadius, 0.25*propRadius, detailRatio * 40, detailRatio * 10, true);

  for (int i = 0; i<propNum; ++i)
  {
    float angle = (float)i * 2.0f * pumex::fpi / (float)propNum;
    pumex::Geometry oneProp;
    oneProp.semantic = vertexSemantic;
    pumex::addCone(oneProp, glm::vec3(0.0, 0.0, -0.9*propRadius), 0.1 * propRadius, 1.0*propRadius, detailRatio * 40, detailRatio * 10, true);

    glm::mat4 matrix = glm::rotate(glm::mat4(), angle, glm::vec3(0.0, 0.0, 1.0)) * glm::scale(glm::mat4(), glm::vec3(1.0, 1.0, 0.3)) * glm::rotate(glm::mat4(), glm::radians(90.0f), glm::vec3(0.0, 1.0, 0.0));
    pumex::transformGeometry(matrix, oneProp);
    uint32_t verticesSoFar = propeller.vertices.size() / oneVertexSize;
    pumex::copyAndConvertVertices(propeller.vertices, propeller.semantic, oneProp.vertices, oneProp.semantic );
    std::transform(oneProp.indices.begin(), oneProp.indices.end(), std::back_inserter(propeller.indices), [=](uint32_t x){ return verticesSoFar + x; });
  }
  result->geometries.push_back(propeller);

  pumex::Skeleton::Bone bone;
  result->skeleton.bones.emplace_back(bone);
  result->skeleton.boneNames.push_back(boneName);
  result->skeleton.invBoneNames.insert({ boneName, 0 });

  return result;
}

pumex::Asset* createBlimp(float detailRatio, const glm::vec4& hullColor, const glm::vec4& propColor)
{
  pumex::Asset* result = new pumex::Asset;
  std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };
  pumex::Skeleton::Bone rootBone;
  result->skeleton.bones.emplace_back(rootBone);
  result->skeleton.boneNames.emplace_back("root");
  result->skeleton.invBoneNames.insert({ "root", 0 });

  pumex::Material hullMaterial;
  hullMaterial.properties["$clr.ambient"] = 0.1f * hullColor;
  hullMaterial.properties["$clr.diffuse"] = 0.9f * hullColor;
  hullMaterial.properties["$clr.specular"] = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  hullMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(hullMaterial);

  pumex::Geometry hull;
  hull.name = "hull";
  hull.semantic = vertexSemantic;
  hull.materialIndex = 0;
  // add main hull
  pumex::addCapsule(hull, glm::vec3(0.0, 0.0, 0.0), 5.0, 10.0, detailRatio * 40, detailRatio * 20, true, true, true);
  // add gondola
  pumex::addCapsule(hull, glm::vec3(5.5, 0.0, 0.0), 1.0, 6.0, detailRatio * 40, detailRatio * 20, true, true, true);
  // add rudders
  pumex::addBox(hull, glm::vec3(-4.0, -0.15, -12.0), glm::vec3(4.0, 0.15, -8.0));
  pumex::addBox(hull, glm::vec3(-0.15, -4.0, -12.0), glm::vec3(0.15, 4.0, -8.0));
  pumex::transformGeometry(glm::rotate(glm::mat4(), glm::radians(90.0f), glm::vec3(0.0, 1.0, 0.0)), hull);
  result->geometries.emplace_back(hull);

  // we add propellers as separate geometries, because they have different materials
  std::shared_ptr<pumex::Asset> propellerLeft ( createPropeller("propL", detailRatio, 4, 1.0, propColor) );
  pumex::Skeleton::Bone transBoneLeft;
  transBoneLeft.parentIndex = 0;
  transBoneLeft.localTransformation = glm::translate(glm::mat4(), glm::vec3(0.0, 2.0, -6.0)) * glm::rotate(glm::mat4(), glm::radians(90.0f), glm::vec3(0.0, 1.0, 0.0));
  uint32_t transBoneLeftIndex = result->skeleton.bones.size();
  result->skeleton.bones.emplace_back(transBoneLeft);
  result->skeleton.boneNames.emplace_back("transBoneLeft");
  result->skeleton.invBoneNames.insert({ "transBoneLeft", transBoneLeftIndex });

  std::shared_ptr<pumex::Asset> propellerRight ( createPropeller("propR", detailRatio, 4, 1.0, propColor) );
  pumex::Skeleton::Bone transBoneRight;
  transBoneRight.parentIndex = 0;
  transBoneRight.localTransformation = glm::translate(glm::mat4(), glm::vec3(0.0, -2.0, -6.0)) * glm::rotate(glm::mat4(), glm::radians(90.0f), glm::vec3(0.0, 1.0, 0.0));
  uint32_t transBoneRightIndex = result->skeleton.bones.size();
  result->skeleton.bones.emplace_back(transBoneRight);
  result->skeleton.boneNames.emplace_back("transBoneRight");
  result->skeleton.invBoneNames.insert({ "transBoneRight", transBoneRightIndex });

  pumex::mergeAsset(*result, transBoneLeftIndex,  *propellerLeft);
  pumex::mergeAsset(*result, transBoneRightIndex, *propellerRight);

  return result;
}

pumex::Asset* createCar(float detailRatio, const glm::vec4& hullColor, const glm::vec4& wheelColor)
{
  pumex::Asset* result = new pumex::Asset;
  std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };
  pumex::Skeleton::Bone rootBone;
  result->skeleton.bones.emplace_back(rootBone);
  result->skeleton.boneNames.emplace_back("root");
  result->skeleton.invBoneNames.insert({ "root", 0 });

  pumex::Material hullMaterial;
  hullMaterial.properties["$clr.ambient"]   = 0.1f * hullColor;
  hullMaterial.properties["$clr.diffuse"]   = 0.9f * hullColor;
  hullMaterial.properties["$clr.specular"]  = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  hullMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(hullMaterial);

  pumex::Geometry hull;
  hull.name          = "hull";
  hull.semantic      = vertexSemantic;
  hull.materialIndex = 0;
  pumex::addBox(hull, glm::vec3(-2.5, -1.5, 0.4), glm::vec3(2.5, 1.5, 2.7));
  result->geometries.emplace_back(hull);

  pumex::Geometry wheel;
  wheel.name          = "wheel";
  wheel.semantic      = vertexSemantic;
  wheel.materialIndex = 0;
  pumex::addCylinder(wheel, glm::vec3(0.0, 0.0, 0.0), 1.0f, 0.6f, detailRatio * 40, true, true, true);
  wheel.indices.pop_back();
  wheel.indices.pop_back();
  wheel.indices.pop_back();

  std::vector<std::shared_ptr<pumex::Asset>> wheels = 
  { 
    std::shared_ptr<pumex::Asset>(pumex::createSimpleAsset(wheel, "wheel0")),
    std::shared_ptr<pumex::Asset>(pumex::createSimpleAsset(wheel, "wheel1")),
    std::shared_ptr<pumex::Asset>(pumex::createSimpleAsset(wheel, "wheel2")),
    std::shared_ptr<pumex::Asset>(pumex::createSimpleAsset(wheel, "wheel3"))
  };
  pumex::Material wheelMaterial;
  wheelMaterial.properties["$clr.ambient"] = 0.1f * wheelColor;
  wheelMaterial.properties["$clr.diffuse"] = 0.9f * wheelColor;
  wheelMaterial.properties["$clr.specular"] = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  wheelMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  for (uint32_t i = 0; i < wheels.size(); ++i)
    wheels[i]->materials.push_back(wheelMaterial);

  std::vector<std::string> wheelNames = { "wheel0", "wheel1", "wheel2", "wheel3" };
  std::vector<glm::mat4> wheelTransformations = 
  {
    glm::translate(glm::mat4(), glm::vec3(2.0, 1.8, 1.0)) * glm::rotate(glm::mat4(), glm::radians(-90.0f), glm::vec3(1.0, 0.0, 0.0)),
    glm::translate(glm::mat4(), glm::vec3(-2.0, 1.8, 1.0)) * glm::rotate(glm::mat4(), glm::radians(-90.0f), glm::vec3(1.0, 0.0, 0.0)),
    glm::translate(glm::mat4(), glm::vec3(2.0, -1.8, 1.0)) * glm::rotate(glm::mat4(), glm::radians(90.0f), glm::vec3(1.0, 0.0, 0.0)),
    glm::translate(glm::mat4(), glm::vec3(-2.0, -1.8, 1.0)) * glm::rotate(glm::mat4(), glm::radians(90.0f), glm::vec3(1.0, 0.0, 0.0))
  };
  std::vector<uint32_t> boneIndices;
  // we add wheels as separate geometries, because they have different materials
  for (uint32_t i = 0; i < wheels.size(); ++i)
  {
    pumex::Skeleton::Bone transBone;
    transBone.parentIndex = 0;
    transBone.localTransformation = wheelTransformations[i];
    uint32_t transBoneIndex = result->skeleton.bones.size();
    boneIndices.push_back(transBoneIndex);
    result->skeleton.bones.emplace_back(transBone);
    result->skeleton.boneNames.emplace_back( wheelNames[i] + "trans" );
    result->skeleton.invBoneNames.insert({ wheelNames[i] + "trans", transBoneIndex });
  }
  for (uint32_t i = 0; i < wheels.size(); ++i)
    pumex::mergeAsset(*result, boneIndices[i], *wheels[i]);

  return result;
}

pumex::Asset* createAirplane(float detailRatio, const glm::vec4& hullColor, const glm::vec4& propColor)
{
  pumex::Asset* result = new pumex::Asset;
  std::vector<pumex::VertexSemantic> vertexSemantic = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };
  pumex::Skeleton::Bone rootBone;
  result->skeleton.bones.emplace_back(rootBone);
  result->skeleton.boneNames.emplace_back("root");
  result->skeleton.invBoneNames.insert({ "root", 0 });

  pumex::Material hullMaterial;
  hullMaterial.properties["$clr.ambient"] = 0.1f * hullColor;
  hullMaterial.properties["$clr.diffuse"] = 0.9f * hullColor;
  hullMaterial.properties["$clr.specular"] = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  hullMaterial.properties["$mat.shininess"] = glm::vec4(128.0f, 0.0f, 0.0f, 0.0f);
  result->materials.push_back(hullMaterial);

  pumex::Geometry hull;
  hull.name = "hull";
  hull.semantic = vertexSemantic;
  hull.materialIndex = 0;
  // add main hull
  pumex::addCapsule(hull, glm::vec3(0.0f, 0.0f, 0.0f), 0.8f, 6.0f, detailRatio * 40, detailRatio * 20, true, true, true);
  // add winds
  pumex::addBox(hull, glm::vec3(0.35, -3.5, 0.5), glm::vec3(0.45, 3.5, 2.1));
  pumex::addBox(hull, glm::vec3(-1.45, -5.0, 0.6), glm::vec3(-1.35, 5.0, 2.4));
  // add rudders
  pumex::addBox(hull, glm::vec3(-1.55, -0.025, -4.4), glm::vec3(-0.05, 0.025, -3.4));
  pumex::addBox(hull, glm::vec3(-0.225, -2.0, -4.4), glm::vec3(-0.175, 2.0, -3.4));
  pumex::transformGeometry(glm::rotate(glm::mat4(), glm::radians(90.0f), glm::vec3(0.0, 1.0, 0.0)), hull);
  result->geometries.emplace_back(hull);

  // we add propeller as separate geometries, because it has different material
  std::shared_ptr<pumex::Asset> propeller(createPropeller("prop", detailRatio, 3, 1.6f, propColor));
  pumex::Skeleton::Bone transBone;
  transBone.parentIndex = 0;
  transBone.localTransformation = glm::translate(glm::mat4(), glm::vec3(3.8, 0.0, 0.0)) * glm::rotate(glm::mat4(), glm::radians(90.0f), glm::vec3(0.0, 1.0, 0.0));

  uint32_t transBoneIndex = result->skeleton.bones.size();
  result->skeleton.bones.emplace_back(transBone);
  result->skeleton.boneNames.emplace_back("transBone");
  result->skeleton.invBoneNames.insert({ "transBone", transBoneIndex });
  pumex::mergeAsset(*result, transBoneIndex, *propeller);

  return result;
}


struct FrameData
{
  FrameData()
  {
  }
  pumex::Camera                       camera;
  std::vector<StaticInstanceData>     staticInstanceData;
  std::vector<DynamicInstanceData>    dynamicInstanceData;
  std::vector<DynamicInstanceDataCPU> dynamicInstanceDataCPU;
};

// struct that works as an application database. Render thread uses data from it
// Look at createStaticRendering() and createDynamicRendering() methods to see how to
// register object types, add procedurally created assets and generate object instances
// Look at update() method to see how dynamic objects are updated.
struct GpuCullCommonData
{
  bool  _showStaticRendering  = true;
  bool  _showDynamicRendering = true;
  uint32_t _instancesPerCell  = 4096;
  float _staticAreaSize       = 2000.0f;
  float _dynamicAreaSize      = 1000.0f;
  float _lodModifier          = 1.0f;
  float _densityModifier      = 1.0f;
  float _triangleModifier     = 1.0f;

  std::weak_ptr<pumex::Viewer>                         viewer;
  std::array<FrameData, 2>                             frameData;
  uint32_t                                             readIdx;
  uint32_t                                             writeIdx;

  std::vector<pumex::VertexSemantic>                   vertexSemantic;
  std::vector<pumex::TextureSemantic>                  textureSemantic;
  std::shared_ptr<pumex::TextureRegistryNull>          textureRegistryNull;

  std::default_random_engine                           randomEngine;

  std::shared_ptr<pumex::AssetBuffer>                  staticAssetBuffer;
  std::shared_ptr<pumex::MaterialSet<MaterialGpuCull>> staticMaterialSet;

  std::shared_ptr<pumex::AssetBuffer>                  dynamicAssetBuffer;
  std::shared_ptr<pumex::MaterialSet<MaterialGpuCull>> dynamicMaterialSet;


  std::shared_ptr<pumex::UniformBuffer<pumex::Camera>>                      cameraUbo;
  std::shared_ptr<pumex::StorageBuffer<StaticInstanceData>>                 staticInstanceSbo;
  std::shared_ptr<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>  staticResultsSbo;
  std::shared_ptr<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>  staticResultsSbo2;
  std::vector<uint32_t>                                                     staticResultsGeomToType;
  std::shared_ptr<pumex::StorageBuffer<uint32_t>>                           staticOffValuesSbo;

  std::shared_ptr<pumex::StorageBuffer<DynamicInstanceData>>                dynamicInstanceSbo;
  std::shared_ptr<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>  dynamicResultsSbo;
  std::shared_ptr<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>  dynamicResultsSbo2;
  std::vector<uint32_t>                                                     dynamicResultsGeomToType;
  std::shared_ptr<pumex::StorageBuffer<uint32_t>>                           dynamicOffValuesSbo;
  uint32_t                                                                  blimpID;
  uint32_t                                                                  carID;
  uint32_t                                                                  airplaneID;
  std::map<uint32_t, std::vector<glm::mat4>>                                bonesReset;


  std::shared_ptr<pumex::RenderPass>                   defaultRenderPass;

  std::shared_ptr<pumex::PipelineCache>                pipelineCache;

  std::shared_ptr<pumex::DescriptorSetLayout>          instancedRenderDescriptorSetLayout;
  std::shared_ptr<pumex::DescriptorPool>               instancedRenderDescriptorPool;
  std::shared_ptr<pumex::PipelineLayout>               instancedRenderPipelineLayout;

  std::shared_ptr<pumex::GraphicsPipeline>             staticRenderPipeline;
  std::shared_ptr<pumex::DescriptorSet>                staticRenderDescriptorSet;

  std::shared_ptr<pumex::GraphicsPipeline>             dynamicRenderPipeline;
  std::shared_ptr<pumex::DescriptorSet>                dynamicRenderDescriptorSet;

  std::shared_ptr<pumex::DescriptorSetLayout>          filterDescriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>               filterPipelineLayout;
  std::shared_ptr<pumex::DescriptorPool>               filterDescriptorPool;

  std::shared_ptr<pumex::ComputePipeline>              staticFilterPipeline;
  std::shared_ptr<pumex::DescriptorSet>                staticFilterDescriptorSet;

  std::shared_ptr<pumex::ComputePipeline>              dynamicFilterPipeline;
  std::shared_ptr<pumex::DescriptorSet>                dynamicFilterDescriptorSet;

  std::shared_ptr<pumex::QueryPool>                    timeStampQueryPool;

  GpuCullCommonData(std::shared_ptr<pumex::Viewer> v)
    : viewer{ v }
  {

  }
  
  void setup(bool showStaticRendering, bool showDynamicRendering, float staticAreaSize, float dynamicAreaSize, float lodModifier, float densityModifier, float triangleModifier)
  {
    _showStaticRendering  = showStaticRendering;
    _showDynamicRendering = showDynamicRendering;
    _instancesPerCell     = 4096;
    _staticAreaSize       = staticAreaSize;
    _dynamicAreaSize      = dynamicAreaSize;
    _lodModifier          = lodModifier;
    _densityModifier      = densityModifier;
    _triangleModifier     = triangleModifier;

    vertexSemantic      = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };
    textureSemantic     = {};
    textureRegistryNull = std::make_shared<pumex::TextureRegistryNull>();

    cameraUbo           = std::make_shared<pumex::UniformBuffer<pumex::Camera>>();
    pipelineCache       = std::make_shared<pumex::PipelineCache>();

    std::vector<pumex::DescriptorSetLayoutBinding> instancedRenderLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
      { 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    instancedRenderDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(instancedRenderLayoutBindings);
    instancedRenderDescriptorPool      = std::make_shared<pumex::DescriptorPool>(2, instancedRenderLayoutBindings);
    instancedRenderPipelineLayout      = std::make_shared<pumex::PipelineLayout>();
    instancedRenderPipelineLayout->descriptorSetLayouts.push_back(instancedRenderDescriptorSetLayout);

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
    filterDescriptorPool = std::make_shared<pumex::DescriptorPool>(2, filterLayoutBindings);
    // building pipeline layout
    filterPipelineLayout = std::make_shared<pumex::PipelineLayout>();
    filterPipelineLayout->descriptorSetLayouts.push_back(filterDescriptorSetLayout);

    if (showStaticRendering)
      createStaticRendering(frameData[0]);

    if (showDynamicRendering)
      createDynamicRendering(frameData[0]);
    frameData[1] = frameData[0];

    timeStampQueryPool = std::make_shared<pumex::QueryPool>(VK_QUERY_TYPE_TIMESTAMP,4*3);

  }

  void createStaticRendering(FrameData& fData)
  {
    std::shared_ptr<pumex::Viewer> viewerSh = viewer.lock();
    CHECK_LOG_THROW(viewerSh.get() == nullptr, "Cannot acces pumex viewer");

    std::vector<uint32_t> objectIDs;

    staticAssetBuffer = std::make_shared<pumex::AssetBuffer>();
    staticAssetBuffer->registerVertexSemantic(1, vertexSemantic);
    staticMaterialSet = std::make_shared<pumex::MaterialSet<MaterialGpuCull>>(viewerSh, textureRegistryNull, textureSemantic);

    std::shared_ptr<pumex::Asset> groundAsset(createGround(_staticAreaSize, glm::vec4(0.0f, 0.7f, 0.0f, 1.0f)));
    pumex::BoundingBox groundBbox = pumex::calculateBoundingBox(*groundAsset, 1);
    uint32_t groundTypeID = staticAssetBuffer->registerType("ground", pumex::AssetTypeDefinition(groundBbox));
    staticMaterialSet->registerMaterials(groundTypeID, groundAsset);
    staticAssetBuffer->registerObjectLOD(groundTypeID, groundAsset, pumex::AssetLodDefinition(0.0f, 5.0f * _staticAreaSize));
    fData.staticInstanceData.push_back(StaticInstanceData(glm::mat4(), groundTypeID, 0, 1.0f, 0.0f, 1.0f, 0.0f));

    std::shared_ptr<pumex::Asset> coniferTree0 ( createConiferTree( 0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> coniferTree1 ( createConiferTree(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> coniferTree2 ( createConiferTree(0.15f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox coniferTreeBbox = pumex::calculateBoundingBox(*coniferTree0, 1);
    uint32_t coniferTreeID = staticAssetBuffer->registerType("coniferTree", pumex::AssetTypeDefinition(coniferTreeBbox));
    staticMaterialSet->registerMaterials(coniferTreeID, coniferTree0);
    staticMaterialSet->registerMaterials(coniferTreeID, coniferTree1);
    staticMaterialSet->registerMaterials(coniferTreeID, coniferTree2);
    staticAssetBuffer->registerObjectLOD(coniferTreeID, coniferTree0, pumex::AssetLodDefinition(  0.0f * _lodModifier,   100.0f * _lodModifier ));
    staticAssetBuffer->registerObjectLOD(coniferTreeID, coniferTree1, pumex::AssetLodDefinition( 100.0f * _lodModifier,  500.0f * _lodModifier ));
    staticAssetBuffer->registerObjectLOD(coniferTreeID, coniferTree2, pumex::AssetLodDefinition( 500.0f * _lodModifier, 1200.0f * _lodModifier ));
    objectIDs.push_back(coniferTreeID);

    std::shared_ptr<pumex::Asset> decidousTree0 ( createDecidousTree(0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> decidousTree1 ( createDecidousTree(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> decidousTree2 ( createDecidousTree(0.15f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox decidousTreeBbox = pumex::calculateBoundingBox(*decidousTree0, 1);
    uint32_t decidousTreeID = staticAssetBuffer->registerType("decidousTree", pumex::AssetTypeDefinition(decidousTreeBbox));
    staticMaterialSet->registerMaterials(decidousTreeID, decidousTree0);
    staticMaterialSet->registerMaterials(decidousTreeID, decidousTree1);
    staticMaterialSet->registerMaterials(decidousTreeID, decidousTree2);
    staticAssetBuffer->registerObjectLOD(decidousTreeID, decidousTree0, pumex::AssetLodDefinition(  0.0f * _lodModifier,   120.0f * _lodModifier ));
    staticAssetBuffer->registerObjectLOD(decidousTreeID, decidousTree1, pumex::AssetLodDefinition( 120.0f * _lodModifier,  600.0f * _lodModifier ));
    staticAssetBuffer->registerObjectLOD(decidousTreeID, decidousTree2, pumex::AssetLodDefinition( 600.0f * _lodModifier, 1400.0f * _lodModifier ));
    objectIDs.push_back(decidousTreeID);

    std::shared_ptr<pumex::Asset> simpleHouse0 ( createSimpleHouse(0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> simpleHouse1 ( createSimpleHouse(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> simpleHouse2 ( createSimpleHouse(0.15f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox simpleHouseBbox = pumex::calculateBoundingBox(*simpleHouse0, 1);
    uint32_t simpleHouseID = staticAssetBuffer->registerType("simpleHouse", pumex::AssetTypeDefinition(simpleHouseBbox));
    staticMaterialSet->registerMaterials(simpleHouseID, simpleHouse0);
    staticMaterialSet->registerMaterials(simpleHouseID, simpleHouse1);
    staticMaterialSet->registerMaterials(simpleHouseID, simpleHouse2);
    staticAssetBuffer->registerObjectLOD(simpleHouseID, simpleHouse0, pumex::AssetLodDefinition(0.0f * _lodModifier, 120.0f * _lodModifier));
    staticAssetBuffer->registerObjectLOD(simpleHouseID, simpleHouse1, pumex::AssetLodDefinition(120.0f * _lodModifier, 600.0f * _lodModifier));
    staticAssetBuffer->registerObjectLOD(simpleHouseID, simpleHouse2, pumex::AssetLodDefinition(600.0f * _lodModifier, 1400.0f * _lodModifier));
    objectIDs.push_back(simpleHouseID);

    staticMaterialSet->refreshMaterialStructures();

    float objectDensity[3]     = { 10000.0f * _densityModifier, 1000.0f * _densityModifier, 100.0f * _densityModifier };
    float amplitudeModifier[3] = { 1.0f, 1.0f, 0.0f }; // we don't want the house to wave in the wind

    float fullArea = _staticAreaSize * _staticAreaSize;
    std::uniform_real_distribution<float>   xAxis(-0.5f*_staticAreaSize, 0.5f * _staticAreaSize);
    std::uniform_real_distribution<float>   yAxis(-0.5f*_staticAreaSize, 0.5f * _staticAreaSize);
    std::uniform_real_distribution<float>   zRot(-180.0f, 180.0f);
    std::uniform_real_distribution<float>   xyzScale(0.8f, 1.2f);
    std::uniform_real_distribution<float>   rBrightness(0.5f, 1.0f);
    std::uniform_real_distribution<float>   rAmplitude(0.01f, 0.05f);
    std::uniform_real_distribution<float>   rFrequency(0.1f * 2.0f * pumex::fpi, 0.5f * 2.0f * pumex::fpi);
    std::uniform_real_distribution<float>   rOffset(0.0f * 2.0f * pumex::fpi, 1.0f * 2.0f * pumex::fpi);

    for (unsigned int i = 0; i<objectIDs.size(); ++i)
    {
      int objectQuantity = (int)floor(objectDensity[i] * fullArea / 1000000.0f);

      for (int j = 0; j<objectQuantity; ++j)
      {
        glm::vec3 pos( xAxis(randomEngine), yAxis(randomEngine), 0.0f );
        float rot             = zRot(randomEngine);
        float scale           = xyzScale(randomEngine);
        float brightness      = rBrightness(randomEngine);
        float wavingAmplitude = rAmplitude(randomEngine) * amplitudeModifier[i];
        float wavingFrequency = rFrequency(randomEngine);
        float wavingOffset    = rOffset(randomEngine);
        glm::mat4 position(glm::translate(glm::mat4(), glm::vec3(pos.x, pos.y, pos.z)) * glm::rotate(glm::mat4(), rot, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::scale(glm::mat4(), glm::vec3(scale, scale, scale)));
        fData.staticInstanceData.push_back(StaticInstanceData(position, objectIDs[i], 0, brightness, wavingAmplitude, wavingFrequency, wavingOffset));
      }
    }

    staticInstanceSbo   = std::make_shared<pumex::StorageBuffer<StaticInstanceData>>();
    staticResultsSbo    = std::make_shared<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    staticResultsSbo2   = std::make_shared<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>((VkBufferUsageFlagBits)(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    staticOffValuesSbo  = std::make_shared<pumex::StorageBuffer<uint32_t>>();


    staticFilterPipeline = std::make_shared<pumex::ComputePipeline>(pipelineCache, filterPipelineLayout);
    staticFilterPipeline->shaderStage = { VK_SHADER_STAGE_COMPUTE_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("gpucull_static_filter_instances.comp.spv")), "main" };

    staticFilterDescriptorSet = std::make_shared<pumex::DescriptorSet>(filterDescriptorSetLayout, filterDescriptorPool);
    staticFilterDescriptorSet->setSource(0, staticAssetBuffer->getTypeBufferDescriptorSetSource(1));
    staticFilterDescriptorSet->setSource(1, staticAssetBuffer->getLODBufferDescriptorSetSource(1));
    staticFilterDescriptorSet->setSource(2, cameraUbo);
    staticFilterDescriptorSet->setSource(3, staticInstanceSbo);
    staticFilterDescriptorSet->setSource(4, staticResultsSbo);
    staticFilterDescriptorSet->setSource(5, staticOffValuesSbo);

    staticRenderPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, instancedRenderPipelineLayout, defaultRenderPass, 0);
    staticRenderPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("gpucull_static_render.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("gpucull_static_render.frag.spv")), "main" }
    };
    staticRenderPipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, vertexSemantic }
    };
    staticRenderPipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    staticRenderPipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    staticRenderDescriptorSet = std::make_shared<pumex::DescriptorSet>(instancedRenderDescriptorSetLayout, instancedRenderDescriptorPool);
    staticRenderDescriptorSet->setSource(0, cameraUbo);
    staticRenderDescriptorSet->setSource(1, staticInstanceSbo);
    staticRenderDescriptorSet->setSource(2, staticOffValuesSbo);
    staticRenderDescriptorSet->setSource(3, staticMaterialSet->getTypeBufferDescriptorSetSource());
    staticRenderDescriptorSet->setSource(4, staticMaterialSet->getMaterialVariantBufferDescriptorSetSource());
    staticRenderDescriptorSet->setSource(5, staticMaterialSet->getMaterialDefinitionBufferDescriptorSetSource());

    std::vector<pumex::DrawIndexedIndirectCommand> results;
    staticAssetBuffer->prepareDrawIndexedIndirectCommandBuffer(1, results, staticResultsGeomToType);
    staticResultsSbo->set(results);
    staticResultsSbo2->set(results);

    // Warning: if you want to change quantity and types of rendered objects then you have to recalculate instance offsets
    staticInstanceSbo->set(fData.staticInstanceData);
    recalculateStaticInstanceOffsets(fData);
  }

  void createDynamicRendering(FrameData& fData)
  {
    std::shared_ptr<pumex::Viewer> viewerSh = viewer.lock();
    CHECK_LOG_THROW(viewerSh.get() == nullptr, "Cannot acces pumex viewer");

    std::vector<uint32_t> objectIDs;

    dynamicAssetBuffer = std::make_shared<pumex::AssetBuffer>();
    dynamicAssetBuffer->registerVertexSemantic(1, vertexSemantic);
    dynamicMaterialSet = std::make_shared<pumex::MaterialSet<MaterialGpuCull>>(viewerSh, textureRegistryNull, textureSemantic);

    std::shared_ptr<pumex::Asset> blimpLod0 ( createBlimp(0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)) );
    std::shared_ptr<pumex::Asset> blimpLod1 ( createBlimp(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)) );
    std::shared_ptr<pumex::Asset> blimpLod2 ( createBlimp(0.20f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)) );
    pumex::BoundingBox blimpBbox = pumex::calculateBoundingBox(*blimpLod0, 1);
    blimpID = dynamicAssetBuffer->registerType("blimp", pumex::AssetTypeDefinition(blimpBbox));
    dynamicMaterialSet->registerMaterials(blimpID, blimpLod0);
    dynamicMaterialSet->registerMaterials(blimpID, blimpLod1);
    dynamicMaterialSet->registerMaterials(blimpID, blimpLod2);
    dynamicAssetBuffer->registerObjectLOD(blimpID, blimpLod0, pumex::AssetLodDefinition(0.0f * _lodModifier, 150.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(blimpID, blimpLod1, pumex::AssetLodDefinition(150.0f * _lodModifier, 800.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(blimpID, blimpLod2, pumex::AssetLodDefinition(800.0f * _lodModifier, 6500.0f * _lodModifier));
    objectIDs.push_back(blimpID);
    bonesReset[blimpID] = pumex::calculateResetPosition(*blimpLod0);

    std::shared_ptr<pumex::Asset> carLod0(createCar(0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.3, 0.3, 0.3, 1.0)));
    std::shared_ptr<pumex::Asset> carLod1(createCar(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> carLod2(createCar(0.15f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox carBbox = pumex::calculateBoundingBox(*carLod0, 1);
    carID = dynamicAssetBuffer->registerType("car", pumex::AssetTypeDefinition(carBbox));
    dynamicMaterialSet->registerMaterials(carID, carLod0);
    dynamicMaterialSet->registerMaterials(carID, carLod1);
    dynamicMaterialSet->registerMaterials(carID, carLod2);
    dynamicAssetBuffer->registerObjectLOD(carID, carLod0, pumex::AssetLodDefinition(0.0f * _lodModifier, 50.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(carID, carLod1, pumex::AssetLodDefinition(50.0f * _lodModifier, 300.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(carID, carLod2, pumex::AssetLodDefinition(300.0f * _lodModifier, 1000.0f * _lodModifier));
    objectIDs.push_back(carID);
    bonesReset[carID] = pumex::calculateResetPosition(*carLod0);

    std::shared_ptr<pumex::Asset> airplaneLod0(createAirplane(0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> airplaneLod1(createAirplane(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> airplaneLod2(createAirplane(0.15f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox airplaneBbox = pumex::calculateBoundingBox(*airplaneLod0, 1);
    airplaneID = dynamicAssetBuffer->registerType("airplane", pumex::AssetTypeDefinition(airplaneBbox));
    dynamicMaterialSet->registerMaterials(airplaneID, airplaneLod0);
    dynamicMaterialSet->registerMaterials(airplaneID, airplaneLod1);
    dynamicMaterialSet->registerMaterials(airplaneID, airplaneLod2);
    dynamicAssetBuffer->registerObjectLOD(airplaneID, airplaneLod0, pumex::AssetLodDefinition(0.0f * _lodModifier, 80.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(airplaneID, airplaneLod1, pumex::AssetLodDefinition(80.0f * _lodModifier, 400.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(airplaneID, airplaneLod2, pumex::AssetLodDefinition(400.0f * _lodModifier, 1200.0f * _lodModifier));
    objectIDs.push_back(airplaneID);
    bonesReset[airplaneID] = pumex::calculateResetPosition(*airplaneLod0);

    dynamicMaterialSet->refreshMaterialStructures();

    float objectZ[3]        = { 50.0f, 0.0f, 25.0f };
    float objectDensity[3]  = { 100.0f * _densityModifier, 100.0f * _densityModifier, 100.0f * _densityModifier };
    float minObjectSpeed[3] = { 5.0f, 1.0f, 10.0f };
    float maxObjectSpeed[3] = { 10.0f, 5.0f, 16.0f };

    float fullArea = _dynamicAreaSize * _dynamicAreaSize;
    std::uniform_real_distribution<float>              randomX(-0.5f*_dynamicAreaSize, 0.5f * _dynamicAreaSize);
    std::uniform_real_distribution<float>              randomY(-0.5f*_dynamicAreaSize, 0.5f * _dynamicAreaSize);
    std::uniform_real_distribution<float>              randomRot(-180.0f, 180.0f);
    std::uniform_real_distribution<float>              randomBrightness(0.5f, 1.0f);
    std::vector<std::uniform_real_distribution<float>> randomObjectSpeed;
    std::exponential_distribution<float>               randomTime2NextTurn(0.1f);
    for (uint32_t i = 0; i<objectIDs.size(); ++i)
      randomObjectSpeed.push_back(std::uniform_real_distribution<float>(minObjectSpeed[i], maxObjectSpeed[i]));

    for (uint32_t i = 0; i<objectIDs.size(); ++i)
    {
      int objectQuantity = (int)floor(objectDensity[i] * fullArea / 1000000.0f);
      for (int j = 0; j<objectQuantity; ++j)
      {
        glm::vec3 pos(randomX(randomEngine), randomY(randomEngine), objectZ[i]);
        float rot           = randomRot(randomEngine);
        float brightness    = randomBrightness(randomEngine);
        float speed         = randomObjectSpeed[i](randomEngine);
        float time2NextTurn = randomTime2NextTurn(randomEngine);

        glm::mat4 position(glm::translate(glm::mat4(), glm::vec3(pos.x, pos.y, pos.z)) * glm::rotate(glm::mat4(), rot, glm::vec3(0.0f, 0.0f, 1.0f)));

        DynamicInstanceDataCPU instanceDataCPU(pos, rot, speed, time2NextTurn);
        DynamicInstanceData instanceData(position, objectIDs[i], 0, brightness);
        for (uint32_t k = 0; k<bonesReset[objectIDs[i]].size() && k<MAX_BONES; ++k)
          instanceData.bones[k] = bonesReset[objectIDs[i]][k];

        fData.dynamicInstanceData.push_back(instanceData);
        fData.dynamicInstanceDataCPU.push_back(instanceDataCPU);
      }
    }

    dynamicInstanceSbo  = std::make_shared<pumex::StorageBuffer<DynamicInstanceData>>();
    dynamicResultsSbo   = std::make_shared<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    dynamicResultsSbo2  = std::make_shared<pumex::StorageBuffer<pumex::DrawIndexedIndirectCommand>>((VkBufferUsageFlagBits)(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    dynamicOffValuesSbo = std::make_shared<pumex::StorageBuffer<uint32_t>>();

    dynamicFilterPipeline = std::make_shared<pumex::ComputePipeline>(pipelineCache, filterPipelineLayout);
    dynamicFilterPipeline->shaderStage = { VK_SHADER_STAGE_COMPUTE_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("gpucull_dynamic_filter_instances.comp.spv")), "main" };

    dynamicFilterDescriptorSet = std::make_shared<pumex::DescriptorSet>(filterDescriptorSetLayout, filterDescriptorPool);
    dynamicFilterDescriptorSet->setSource(0, dynamicAssetBuffer->getTypeBufferDescriptorSetSource(1));
    dynamicFilterDescriptorSet->setSource(1, dynamicAssetBuffer->getLODBufferDescriptorSetSource(1));
    dynamicFilterDescriptorSet->setSource(2, cameraUbo);
    dynamicFilterDescriptorSet->setSource(3, dynamicInstanceSbo);
    dynamicFilterDescriptorSet->setSource(4, dynamicResultsSbo);
    dynamicFilterDescriptorSet->setSource(5, dynamicOffValuesSbo);

    dynamicRenderPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, instancedRenderPipelineLayout, defaultRenderPass, 0);
    dynamicRenderPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("gpucull_dynamic_render.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("gpucull_dynamic_render.frag.spv")), "main" }
    };
    dynamicRenderPipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, vertexSemantic }
    };
    dynamicRenderPipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    dynamicRenderPipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    dynamicRenderDescriptorSet = std::make_shared<pumex::DescriptorSet>(instancedRenderDescriptorSetLayout, instancedRenderDescriptorPool);
    dynamicRenderDescriptorSet->setSource(0, cameraUbo);
    dynamicRenderDescriptorSet->setSource(1, dynamicInstanceSbo);
    dynamicRenderDescriptorSet->setSource(2, dynamicOffValuesSbo);
    dynamicRenderDescriptorSet->setSource(3, dynamicMaterialSet->getTypeBufferDescriptorSetSource());
    dynamicRenderDescriptorSet->setSource(4, dynamicMaterialSet->getMaterialVariantBufferDescriptorSetSource());
    dynamicRenderDescriptorSet->setSource(5, dynamicMaterialSet->getMaterialDefinitionBufferDescriptorSetSource());

    std::vector<pumex::DrawIndexedIndirectCommand> results;
    dynamicAssetBuffer->prepareDrawIndexedIndirectCommandBuffer(1, results, dynamicResultsGeomToType);
    dynamicResultsSbo->set(results);
    dynamicResultsSbo2->set(results);

    // Warning: if you want to change quantity and types of rendered objects then you have to recalculate instance offsets
    dynamicInstanceSbo->set(fData.dynamicInstanceData);
    recalculateDynamicInstanceOffsets(fData);
  }

  void recalculateStaticInstanceOffsets(FrameData& fData)
  {
    std::vector<uint32_t> typeCount(staticAssetBuffer->getNumTypesID());
    std::fill(typeCount.begin(), typeCount.end(), 0);

    // compute how many instances of each type there is
    for (uint32_t i = 0; i<fData.staticInstanceData.size(); ++i)
      typeCount[fData.staticInstanceData[i].typeID]++;

    std::vector<uint32_t> offsets;
    for (uint32_t i = 0; i<staticResultsGeomToType.size(); ++i)
      offsets.push_back(typeCount[staticResultsGeomToType[i]]);

    std::vector<pumex::DrawIndexedIndirectCommand> results = staticResultsSbo->get();
    uint32_t offsetSum = 0;
    for (uint32_t i = 0; i<offsets.size(); ++i)
    {
      uint32_t tmp = offsetSum;
      offsetSum += offsets[i];
      offsets[i] = tmp;
      results[i].firstInstance = tmp;
    }
    staticResultsSbo->set(results);
    staticOffValuesSbo->set(std::vector<uint32_t>(offsetSum));
  }

  void recalculateDynamicInstanceOffsets(FrameData& fData)
  {
    std::vector<uint32_t> typeCount(dynamicAssetBuffer->getNumTypesID());
    std::fill(typeCount.begin(), typeCount.end(), 0);

    // compute how many instances of each type there is
    for (uint32_t i = 0; i<fData.dynamicInstanceData.size(); ++i)
      typeCount[fData.dynamicInstanceData[i].typeID]++;

    std::vector<uint32_t> offsets;
    for (uint32_t i = 0; i<dynamicResultsGeomToType.size(); ++i)
      offsets.push_back(typeCount[dynamicResultsGeomToType[i]]);

    std::vector<pumex::DrawIndexedIndirectCommand> results = dynamicResultsSbo->get();
    uint32_t offsetSum = 0;
    for (uint32_t i = 0; i<offsets.size(); ++i)
    {
      uint32_t tmp = offsetSum;
      offsetSum += offsets[i];
      offsets[i] = tmp;
      results[i].firstInstance = tmp;
    }
    dynamicResultsSbo->set(results);
    dynamicOffValuesSbo->set(std::vector<uint32_t>(offsetSum));
  }


  void update(double timeSinceStart, double timeSinceLastFrame)
  {
    if (_showStaticRendering)
    {
      // reset values to 0
      staticResultsSbo->setDirty();
    }
    if (_showDynamicRendering)
    {
      std::exponential_distribution<float>  randomTime2NextTurn(0.1f);
      std::uniform_real_distribution<float> randomRotation(-180.0f, 180.0f);
      glm::vec2 minArea(-0.5f*_dynamicAreaSize, -0.5f*_dynamicAreaSize);
      glm::vec2 maxArea(0.5f*_dynamicAreaSize, 0.5f*_dynamicAreaSize);

      std::shared_ptr<pumex::Asset> blimpAsset    = dynamicAssetBuffer->getAsset(blimpID, 0);
      uint32_t blimpPropL                         = blimpAsset->skeleton.invBoneNames["propL"];
      uint32_t blimpPropR                         = blimpAsset->skeleton.invBoneNames["propR"];

      std::shared_ptr<pumex::Asset> carAsset      = dynamicAssetBuffer->getAsset(carID, 0);
      uint32_t carWheel0                          = carAsset->skeleton.invBoneNames["wheel0"];
      uint32_t carWheel1                          = carAsset->skeleton.invBoneNames["wheel1"];
      uint32_t carWheel2                          = carAsset->skeleton.invBoneNames["wheel2"];
      uint32_t carWheel3                          = carAsset->skeleton.invBoneNames["wheel3"];

      std::shared_ptr<pumex::Asset> airplaneAsset = dynamicAssetBuffer->getAsset(airplaneID, 0);
      uint32_t airplaneProp                       = airplaneAsset->skeleton.invBoneNames["prop"];

      for (uint32_t i = 0; i<frameData[readIdx].dynamicInstanceData.size(); ++i)
      {
        updateInstance
        (
          frameData[readIdx].dynamicInstanceData[i], frameData[readIdx].dynamicInstanceDataCPU[i],
          frameData[writeIdx].dynamicInstanceData[i], frameData[writeIdx].dynamicInstanceDataCPU[i],
          timeSinceStart, timeSinceLastFrame
        );
      }

      // reset values to 0
      dynamicResultsSbo->setDirty();
      dynamicInstanceSbo->set(dynamicInstanceData);
      // if you changed types or quantity of objects in dynamicInstanceData then you need to ...
//      recalculateDynamicInstanceOffsets();
    }
  }
  void updateInstance(const DynamicInstanceData& inInstanceData, const DynamicInstanceDataCPU& inInstanceDataCPU,
    DynamicInstanceData& outInstanceData, DynamicInstanceDataCPU& outInstanceDataCPU,
    double timeSinceStart, double timeSinceLastFrame)
  {
    // change direction if bot is leaving designated area
    // change direction if bot is leaving designated area
    bool isOutside[] =
    {
      dynamicInstanceDataCPU[i].position.x < minArea.x,
      dynamicInstanceDataCPU[i].position.x > maxArea.x,
      dynamicInstanceDataCPU[i].position.y < minArea.y,
      dynamicInstanceDataCPU[i].position.y > maxArea.y
    };
    if (isOutside[0] || isOutside[1] || isOutside[2] || isOutside[3])
    {
      dynamicInstanceDataCPU[i].position.x = std::max(dynamicInstanceDataCPU[i].position.x, minArea.x);
      dynamicInstanceDataCPU[i].position.x = std::min(dynamicInstanceDataCPU[i].position.x, maxArea.x);
      dynamicInstanceDataCPU[i].position.y = std::max(dynamicInstanceDataCPU[i].position.y, minArea.y);
      dynamicInstanceDataCPU[i].position.y = std::min(dynamicInstanceDataCPU[i].position.y, maxArea.y);
      glm::mat4 rotationMatrix = glm::rotate(glm::mat4(), glm::radians(dynamicInstanceDataCPU[i].rotation), glm::vec3(0.0f, 0.0f, 1.0f));
      glm::vec4 direction = rotationMatrix *  glm::vec4(1, 0, 0, 1); // models move along x axis
      if (isOutside[0] || isOutside[1])
        direction.x *= -1.0f;
      if (isOutside[2] || isOutside[3])
        direction.y *= -1.0f;
      dynamicInstanceDataCPU[i].rotation = glm::degrees(atan2f(direction.y, direction.x));
      dynamicInstanceDataCPU[i].time2NextTurn = randomTime2NextTurn(randomEngine);
    }
    // change rotation, animation and speed if bot requires it
    dynamicInstanceDataCPU[i].time2NextTurn -= timeSinceLastFrame;
    if (dynamicInstanceDataCPU[i].time2NextTurn < 0.0f)
    {
      dynamicInstanceDataCPU[i].rotation = randomRotation(randomEngine);
      dynamicInstanceDataCPU[i].time2NextTurn = randomTime2NextTurn(randomEngine);
    }
    // calculate new position
    glm::mat4 rotationMatrix = glm::rotate(glm::mat4(), dynamicInstanceDataCPU[i].rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec4 direction = rotationMatrix * glm::vec4(1, 0, 0, 1);
    glm::vec3 dir3(direction.x, direction.y, 0.0f);
    dynamicInstanceDataCPU[i].position += dir3 * dynamicInstanceDataCPU[i].speed * float(timeSinceLastFrame);
    dynamicInstanceData[i].position = glm::translate(glm::mat4(), dynamicInstanceDataCPU[i].position) * rotationMatrix;

    // calculate new positions for wheels and propellers
    if (dynamicInstanceData[i].typeID == blimpID)
    {
      dynamicInstanceData[i].bones[blimpPropL] = bonesReset[dynamicInstanceData[i].typeID][blimpPropL] * glm::rotate(glm::mat4(), fmodf(2.0f * pumex::fpi *  0.5f * timeSinceStart, 2.0f*pumex::fpi), glm::vec3(0.0, 0.0, 1.0));
      dynamicInstanceData[i].bones[blimpPropR] = bonesReset[dynamicInstanceData[i].typeID][blimpPropR] * glm::rotate(glm::mat4(), fmodf(2.0f * pumex::fpi * -0.5f * timeSinceStart, 2.0f*pumex::fpi), glm::vec3(0.0, 0.0, 1.0));
    }
    if (dynamicInstanceData[i].typeID == carID)
    {
      dynamicInstanceData[i].bones[carWheel0] = bonesReset[dynamicInstanceData[i].typeID][carWheel0] * glm::rotate(glm::mat4(), fmodf((dynamicInstanceDataCPU[i].speed / 0.5f) * timeSinceStart, 2.0f*pumex::fpi), glm::vec3(0.0, 0.0, 1.0));
      dynamicInstanceData[i].bones[carWheel1] = bonesReset[dynamicInstanceData[i].typeID][carWheel1] * glm::rotate(glm::mat4(), fmodf((dynamicInstanceDataCPU[i].speed / 0.5f) * timeSinceStart, 2.0f*pumex::fpi), glm::vec3(0.0, 0.0, 1.0));
      dynamicInstanceData[i].bones[carWheel2] = bonesReset[dynamicInstanceData[i].typeID][carWheel2] * glm::rotate(glm::mat4(), fmodf((-dynamicInstanceDataCPU[i].speed / 0.5f) * timeSinceStart, 2.0f*pumex::fpi), glm::vec3(0.0, 0.0, 1.0));
      dynamicInstanceData[i].bones[carWheel3] = bonesReset[dynamicInstanceData[i].typeID][carWheel3] * glm::rotate(glm::mat4(), fmodf((-dynamicInstanceDataCPU[i].speed / 0.5f) * timeSinceStart, 2.0f*pumex::fpi), glm::vec3(0.0, 0.0, 1.0));
    }
    if (dynamicInstanceData[i].typeID == airplaneID)
    {
      dynamicInstanceData[i].bones[airplaneProp] = bonesReset[dynamicInstanceData[i].typeID][airplaneProp] * glm::rotate(glm::mat4(), fmodf(2.0f * pumex::fpi *  -1.5f * timeSinceStart, 2.0f*pumex::fpi), glm::vec3(0.0, 0.0, 1.0));
    }
  }


};

// thread that renders data to a Vulkan surface
class GpuCullRenderThread : public pumex::SurfaceThread
{
public:
  GpuCullRenderThread( std::shared_ptr<GpuCullCommonData> GpuCullCommonData )
    : pumex::SurfaceThread(), appData(GpuCullCommonData)
  {
  }

  void setup(std::shared_ptr<pumex::Surface> s) override
  {
    SurfaceThread::setup(s);

    std::shared_ptr<pumex::Surface> surfaceSh = surface.lock();
    std::shared_ptr<pumex::Device>  deviceSh  = surfaceSh->device.lock();
    VkDevice                        vkDevice  = deviceSh->device;

    myCmdBuffer = std::make_shared<pumex::CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, surfaceSh->commandPool);
    myCmdBuffer->validate(deviceSh);

    appData->pipelineCache->validate(deviceSh);
    appData->instancedRenderDescriptorSetLayout->validate(deviceSh);
    appData->instancedRenderDescriptorPool->validate(deviceSh);
    appData->instancedRenderPipelineLayout->validate(deviceSh);
    appData->filterDescriptorSetLayout->validate(deviceSh);
    appData->filterDescriptorPool->validate(deviceSh);
    appData->filterPipelineLayout->validate(deviceSh);
    appData->timeStampQueryPool->validate(deviceSh);

    appData->cameraUbo->validate(deviceSh);

    if (appData->_showStaticRendering)
    {
      appData->staticAssetBuffer->validate(deviceSh, true, surfaceSh->commandPool, surfaceSh->presentationQueue);
      appData->staticMaterialSet->validate(deviceSh, surfaceSh->commandPool, surfaceSh->presentationQueue);
      appData->staticRenderPipeline->validate(deviceSh);
      appData->staticFilterPipeline->validate(deviceSh);

      appData->staticInstanceSbo->validate(deviceSh);
      appData->staticResultsSbo->validate(deviceSh);
      appData->staticResultsSbo2->validate(deviceSh);
      appData->staticOffValuesSbo->validate(deviceSh);
    }

    if (appData->_showDynamicRendering)
    {
      appData->dynamicAssetBuffer->validate(deviceSh, true, surfaceSh->commandPool, surfaceSh->presentationQueue);
      appData->dynamicMaterialSet->validate(deviceSh, surfaceSh->commandPool, surfaceSh->presentationQueue);
      appData->dynamicRenderPipeline->validate(deviceSh);
      appData->dynamicFilterPipeline->validate(deviceSh);

      appData->dynamicInstanceSbo->validate(deviceSh);
      appData->dynamicResultsSbo->validate(deviceSh);
      appData->dynamicResultsSbo2->validate(deviceSh);
      appData->dynamicOffValuesSbo->validate(deviceSh);
    }

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
  ~GpuCullRenderThread()
  {
    cleanup();
  }
  void draw()
  {
    std::shared_ptr<pumex::Surface> surfaceSh = surface.lock();
    std::shared_ptr<pumex::Viewer>  viewerSh  = surface.lock()->viewer.lock();
    std::shared_ptr<pumex::Device>  deviceSh = surfaceSh->device.lock();
    std::shared_ptr<pumex::Window>  windowSh  = surfaceSh->window.lock();
    VkDevice                        vkDevice  = deviceSh->device;

    double timeSinceStartInSeconds = std::chrono::duration<double, std::ratio<1,1>>(timeSinceStart).count();
    double lastFrameInSeconds      = std::chrono::duration<double, std::ratio<1,1>>(timeSinceLastFrame).count();

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
    glm::mat4 viewMatrix = glm::lookAt(eye + cameraPosition, cameraPosition, glm::vec3(0, 0, 1));

    uint32_t renderWidth  = surfaceSh->swapChainSize.width;
    uint32_t renderHeight = surfaceSh->swapChainSize.height;

    pumex::Camera camera = appData->cameraUbo->get();
    camera.setViewMatrix(viewMatrix);
    camera.setObserverPosition(eye + cameraPosition);
    camera.setProjectionMatrix(glm::perspective(glm::radians(60.0f), (float)renderWidth / (float)renderHeight, 0.1f, 100000.0f));
    camera.setTimeSinceStart(timeSinceStartInSeconds);
    appData->cameraUbo->set(camera);

#if defined(GPU_CULL_MEASURE_TIME)
    auto updateStart = pumex::HPClock::now();
#endif
    appData->update(timeSinceStartInSeconds, lastFrameInSeconds);
#if defined(GPU_CULL_MEASURE_TIME)
    auto updateEnd = pumex::HPClock::now();
    double updateDuration = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
#endif
    appData->cameraUbo->validate(deviceSh);

    if (appData->_showStaticRendering)
    {
      appData->staticInstanceSbo->validate(deviceSh);
      appData->staticResultsSbo->validate(deviceSh);
      appData->staticOffValuesSbo->validate(deviceSh);

      appData->staticRenderDescriptorSet->validate(deviceSh);
      appData->staticFilterDescriptorSet->validate(deviceSh);
    }

    if (appData->_showDynamicRendering)
    {
      appData->dynamicInstanceSbo->validate(deviceSh);
      appData->dynamicResultsSbo->validate(deviceSh);
      appData->dynamicOffValuesSbo->validate(deviceSh);

      appData->dynamicRenderDescriptorSet->validate(deviceSh);
      appData->dynamicFilterDescriptorSet->validate(deviceSh);
    }
#if defined(GPU_CULL_MEASURE_TIME)
    auto drawStart = pumex::HPClock::now();
#endif

    myCmdBuffer->cmdBegin(deviceSh);

    appData->timeStampQueryPool->reset(deviceSh, myCmdBuffer, surfaceSh->swapChainImageIndex*4,4);

#if defined(GPU_CULL_MEASURE_TIME)
    appData->timeStampQueryPool->queryTimeStamp(deviceSh, myCmdBuffer, surfaceSh->swapChainImageIndex * 4 + 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif
    pumex::DescriptorSetValue staticResultsBuffer, staticResultsBuffer2, dynamicResultsBuffer, dynamicResultsBuffer2;
    uint32_t staticDrawCount, dynamicDrawCount;

    // Set up memory barrier to ensure that the indirect commands have been consumed before the compute shaders update them
    std::vector<pumex::PipelineBarrier> beforeBufferBarriers;
    if (appData->_showStaticRendering)
    {
      staticResultsBuffer  = appData->staticResultsSbo->getDescriptorSetValue(vkDevice);
      staticResultsBuffer2 = appData->staticResultsSbo2->getDescriptorSetValue(vkDevice);
      staticDrawCount      = appData->staticResultsSbo->get().size();
      beforeBufferBarriers.emplace_back(pumex::PipelineBarrier(VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, surfaceSh->presentationQueueFamilyIndex, surfaceSh->presentationQueueFamilyIndex, staticResultsBuffer.bufferInfo));
    }
    if (appData->_showDynamicRendering)
    {
      dynamicResultsBuffer  = appData->dynamicResultsSbo->getDescriptorSetValue(vkDevice);
      dynamicResultsBuffer2 = appData->dynamicResultsSbo2->getDescriptorSetValue(vkDevice);
      dynamicDrawCount      = appData->dynamicResultsSbo->get().size();
      beforeBufferBarriers.emplace_back(pumex::PipelineBarrier(VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, surfaceSh->presentationQueueFamilyIndex, surfaceSh->presentationQueueFamilyIndex, dynamicResultsBuffer.bufferInfo));
    }
    myCmdBuffer->cmdPipelineBarrier(deviceSh, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, beforeBufferBarriers);

    // perform compute shaders
    if (appData->_showStaticRendering)
    {
      myCmdBuffer->cmdBindPipeline(deviceSh, appData->staticFilterPipeline);
      myCmdBuffer->cmdBindDescriptorSets(deviceSh, VK_PIPELINE_BIND_POINT_COMPUTE, appData->filterPipelineLayout, 0, appData->staticFilterDescriptorSet);
      myCmdBuffer->cmdDispatch(deviceSh, appData->staticInstanceData.size() / 16 + ((appData->staticInstanceData.size() % 16>0) ? 1 : 0), 1, 1);
    }
    if (appData->_showDynamicRendering)
    {
      myCmdBuffer->cmdBindPipeline(deviceSh, appData->dynamicFilterPipeline);
      myCmdBuffer->cmdBindDescriptorSets(deviceSh, VK_PIPELINE_BIND_POINT_COMPUTE, appData->filterPipelineLayout, 0, appData->dynamicFilterDescriptorSet);
      myCmdBuffer->cmdDispatch(deviceSh, appData->dynamicInstanceData.size() / 16 + ((appData->dynamicInstanceData.size() % 16>0) ? 1 : 0), 1, 1);
    }

    // setup memory barriers, so that copying data to *resultsSbo2 will start only after compute shaders finish working
    std::vector<pumex::PipelineBarrier> afterBufferBarriers;
    if (appData->_showStaticRendering)
      afterBufferBarriers.emplace_back(pumex::PipelineBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, surfaceSh->presentationQueueFamilyIndex, surfaceSh->presentationQueueFamilyIndex, staticResultsBuffer.bufferInfo));
    if (appData->_showDynamicRendering)
      afterBufferBarriers.emplace_back(pumex::PipelineBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, surfaceSh->presentationQueueFamilyIndex, surfaceSh->presentationQueueFamilyIndex, dynamicResultsBuffer.bufferInfo));
    myCmdBuffer->cmdPipelineBarrier(deviceSh, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, afterBufferBarriers);

    if (appData->_showStaticRendering)
    {
      VkBufferCopy copyRegion{};
        copyRegion.srcOffset = staticResultsBuffer.bufferInfo.offset;
        copyRegion.size      = staticResultsBuffer.bufferInfo.range;
        copyRegion.dstOffset = staticResultsBuffer2.bufferInfo.offset;
      myCmdBuffer->cmdCopyBuffer(deviceSh, staticResultsBuffer.bufferInfo.buffer, staticResultsBuffer2.bufferInfo.buffer, copyRegion);
    }
    if (appData->_showDynamicRendering)
    {
      VkBufferCopy copyRegion{};
        copyRegion.srcOffset = dynamicResultsBuffer.bufferInfo.offset;
        copyRegion.size      = dynamicResultsBuffer.bufferInfo.range;
        copyRegion.dstOffset = dynamicResultsBuffer2.bufferInfo.offset;
      myCmdBuffer->cmdCopyBuffer(deviceSh, dynamicResultsBuffer.bufferInfo.buffer, dynamicResultsBuffer2.bufferInfo.buffer, copyRegion);
    }
    
    // wait until copying finishes before rendering data  
    std::vector<pumex::PipelineBarrier> afterCopyBufferBarriers;
    if (appData->_showStaticRendering)
      afterCopyBufferBarriers.emplace_back(pumex::PipelineBarrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, surfaceSh->presentationQueueFamilyIndex, surfaceSh->presentationQueueFamilyIndex, staticResultsBuffer2.bufferInfo));
    if (appData->_showDynamicRendering)
      afterCopyBufferBarriers.emplace_back(pumex::PipelineBarrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, surfaceSh->presentationQueueFamilyIndex, surfaceSh->presentationQueueFamilyIndex, dynamicResultsBuffer2.bufferInfo));
    myCmdBuffer->cmdPipelineBarrier(deviceSh, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, afterCopyBufferBarriers);

#if defined(GPU_CULL_MEASURE_TIME)
    appData->timeStampQueryPool->queryTimeStamp(deviceSh, myCmdBuffer, surfaceSh->swapChainImageIndex * 4 + 1, VK_PIPELINE_STAGE_TRANSFER_BIT);
#endif

    std::vector<VkClearValue> clearValues = { pumex::makeColorClearValue(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), pumex::makeDepthStencilClearValue(1.0f, 0) };
    myCmdBuffer->cmdBeginRenderPass(deviceSh, appData->defaultRenderPass, surfaceSh->getCurrentFrameBuffer(), pumex::makeVkRect2D(0, 0, renderWidth, renderHeight), clearValues);
    myCmdBuffer->cmdSetViewport( deviceSh, 0, { pumex::makeViewport(0, 0, renderWidth, renderHeight, 0.0f, 1.0f ) } );
    myCmdBuffer->cmdSetScissor( deviceSh, 0, { pumex::makeVkRect2D( 0, 0, renderWidth, renderHeight ) } );

#if defined(GPU_CULL_MEASURE_TIME)
    appData->timeStampQueryPool->queryTimeStamp(deviceSh, myCmdBuffer, surfaceSh->swapChainImageIndex * 4 + 2, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
#endif
    if (appData->_showStaticRendering)
    {
      myCmdBuffer->cmdBindPipeline(deviceSh, appData->staticRenderPipeline);
      myCmdBuffer->cmdBindDescriptorSets(deviceSh, VK_PIPELINE_BIND_POINT_GRAPHICS, appData->instancedRenderPipelineLayout, 0, appData->staticRenderDescriptorSet);
      appData->staticAssetBuffer->cmdBindVertexIndexBuffer(deviceSh, myCmdBuffer, 1, 0);
      if (deviceSh->physical.lock()->features.multiDrawIndirect == 1)
        myCmdBuffer->cmdDrawIndexedIndirect(deviceSh, staticResultsBuffer2.bufferInfo.buffer, staticResultsBuffer2.bufferInfo.offset, staticDrawCount, sizeof(pumex::DrawIndexedIndirectCommand));
      else
      {
        for (uint32_t i = 0; i < staticDrawCount; ++i)
          myCmdBuffer->cmdDrawIndexedIndirect(deviceSh, staticResultsBuffer2.bufferInfo.buffer, staticResultsBuffer2.bufferInfo.offset + i*sizeof(pumex::DrawIndexedIndirectCommand), 1, sizeof(pumex::DrawIndexedIndirectCommand));
      }
    }
    if (appData->_showDynamicRendering)
    {
      myCmdBuffer->cmdBindPipeline(deviceSh, appData->dynamicRenderPipeline);
      myCmdBuffer->cmdBindDescriptorSets(deviceSh, VK_PIPELINE_BIND_POINT_GRAPHICS, appData->instancedRenderPipelineLayout, 0, appData->dynamicRenderDescriptorSet);
      appData->dynamicAssetBuffer->cmdBindVertexIndexBuffer(deviceSh, myCmdBuffer, 1, 0);
      if (deviceSh->physical.lock()->features.multiDrawIndirect == 1)
        myCmdBuffer->cmdDrawIndexedIndirect(deviceSh, dynamicResultsBuffer2.bufferInfo.buffer, dynamicResultsBuffer2.bufferInfo.offset, dynamicDrawCount, sizeof(pumex::DrawIndexedIndirectCommand));
      else
      {
        for (uint32_t i = 0; i < dynamicDrawCount; ++i)
          myCmdBuffer->cmdDrawIndexedIndirect(deviceSh, dynamicResultsBuffer2.bufferInfo.buffer, dynamicResultsBuffer2.bufferInfo.offset + i*sizeof(pumex::DrawIndexedIndirectCommand), 1, sizeof(pumex::DrawIndexedIndirectCommand));
      }
    }
#if defined(GPU_CULL_MEASURE_TIME)
    appData->timeStampQueryPool->queryTimeStamp(deviceSh, myCmdBuffer, surfaceSh->swapChainImageIndex * 4 + 3, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
#endif

    myCmdBuffer->cmdEndRenderPass(deviceSh);
    myCmdBuffer->cmdEnd(deviceSh);
    myCmdBuffer->queueSubmit(deviceSh, surfaceSh->presentationQueue, { surface.lock()->imageAvailableSemaphore }, { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }, { surface.lock()->renderCompleteSemaphore }, VK_NULL_HANDLE);

#if defined(GPU_CULL_MEASURE_TIME)
    auto drawEnd = pumex::HPClock::now();
    double drawDuration = std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();
    LOG_ERROR << "Frame time                : " << 1000.0 * lastFrameInSeconds << " ms ( FPS = " << 1.0 / lastFrameInSeconds << " )" << std::endl;
    LOG_ERROR << "Update duration           : " << updateDuration << " ms" << std::endl;
    LOG_ERROR << "Fill cmdBuffer duration   : " << drawDuration << " ms" << std::endl;

    float timeStampPeriod = deviceSh->physical.lock()->properties.limits.timestampPeriod / 1000000.0f;
    std::vector<uint64_t> queryResults;
    // We use swapChainImageIndex to get the time measurments from previous frame - timeStampQueryPool works like circular buffer
    queryResults = appData->timeStampQueryPool->getResults(deviceSh, ((surfaceSh->swapChainImageIndex + 2) % 3) * 4, 4, 0);
    LOG_ERROR << "GPU compute duration      : " << (queryResults[1] - queryResults[0]) * timeStampPeriod  << " ms" << std::endl;
    LOG_ERROR << "GPU draw duration         : " << (queryResults[3] - queryResults[2]) * timeStampPeriod << " ms" << std::endl;
    LOG_ERROR << std::endl;

#endif
  }

  std::shared_ptr<GpuCullCommonData> appData;
  std::shared_ptr<pumex::CommandBuffer> myCmdBuffer;

  glm::vec3 cameraPosition;
  glm::vec2 cameraGeographicCoordinates;
  float     cameraDistance;
  glm::vec2 lastMousePos;
  bool      leftMouseKeyPressed;
  bool      rightMouseKeyPressed;
};

int main(int argc, char * argv[])
{
  SET_LOG_INFO;
  LOG_INFO << "Object culling on GPU" << std::endl;

  // Later I will move these parameters to a command line as in osggpucull example
  bool  showStaticRendering  = true;
  bool  showDynamicRendering = true;
  float staticAreaSize       = 2000.0f;
  float dynamicAreaSize      = 1000.0f;
  float lodModifier          = 1.0f;  // lod distances are multiplied by this parameter
  float densityModifier      = 1.0f;  // 
  float triangleModifier     = 1.0f;
	
  // Below is the definition of Vulkan instance, devices, queues, surfaces, windows, render passes and render threads. All in one place - with all parameters listed
  const std::vector<std::string> requestDebugLayers = { { "VK_LAYER_LUNARG_standard_validation" } };
  pumex::ViewerTraits viewerTraits{ "Gpu cull comparison", true, requestDebugLayers };
  viewerTraits.debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT;// | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;

  std::shared_ptr<pumex::Viewer> viewer = std::make_shared<pumex::Viewer>(viewerTraits);
  try
  {
    std::vector<pumex::QueueTraits> requestQueues = { { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, { 0.75f } } };
    std::vector<const char*> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device = viewer->addDevice(0, requestQueues, requestDeviceExtensions);
    CHECK_LOG_THROW(!device->isValid(), "Cannot create logical device with requested parameters" );

    pumex::WindowTraits windowTraits{0, 100, 100, 640, 480, false, "Object culling on GPU"};
    std::shared_ptr<pumex::Window> window = pumex::Window::createWindow(windowTraits);

    pumex::SurfaceTraits surfaceTraits{ 3, VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, VK_FORMAT_D24_UNORM_S8_UINT, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
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

    std::shared_ptr<GpuCullCommonData> gpuCullCommonData = std::make_shared<GpuCullCommonData>(viewer);
    gpuCullCommonData->defaultRenderPass = renderPass;
    gpuCullCommonData->setup(showStaticRendering, showDynamicRendering, staticAreaSize, dynamicAreaSize, lodModifier, densityModifier, triangleModifier);

    std::shared_ptr<pumex::SurfaceThread> thread0 = std::make_shared<GpuCullRenderThread>(gpuCullCommonData);
    std::shared_ptr<pumex::Surface> surface = viewer->addSurface(window, device, surfaceTraits, thread0);

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
