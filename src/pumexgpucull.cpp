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
#include <iomanip>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <gli/gli.hpp>
#include <tbb/tbb.h>
#include <pumex/Pumex.h>
#include <pumex/AssetLoaderAssimp.h>
#include <pumex/utils/Shapes.h>
// suppression of noexcept keyword used in args library ( so that code may compile on VS 2013 )
#ifdef _MSC_VER
  #if _MSC_VER<1900
    #define noexcept 
  #endif
#endif
#include <args.hxx>


// This example shows how to render multiple different objects using a minimal number of vkCmdDrawIndexedIndirect commands
// ( the number of draw calls is equal to number of rendered object types ).
//
// Rendering consists of following parts :
// 1. Positions and parameters of all objects are sent to compute shader. Compute shader ( a filter ) culls invisible objects using 
//    camera parameters, object position and object bounding box. For visible objects the appropriate level of detail is chosen. 
//    Results are stored in a buffer.
// 2. Above mentioned buffer is used during rendering to choose appropriate object parameters ( position, bone matrices, object specific parameters, material ids, etc )
// 
// Demo presents possibility to render both static and dynamic objects :
// - static objects consist mainly of trees, so animation of waving in the wind was added ( amplitude of waving was set to 0 for buildings :) ).
// - in this example all static objects are sent at once ( that's why compute shader takes so much time - compare it to 500 people rendered in crowd example ). 
//   In real application CPU would only sent objects that are visible to a user. Such objects would be stored in some form of quad tree
// - dynamic objects present the possibility to animate object parts of an object ( wheels, propellers ) 
// - static and dynamic object use different set of rendering parameters : compare StaticInstanceData and DynamicInstanceData structures
//
// pumexgpucull example is a copy of similar program that I created for OpenSceneGraph engine few years ago ( osggpucull example ), so you may
// compare Vulkan and OpenGL performance ( I used ordinary graphics shaders instead of compute shaders in OpenGL demo, but performance of rendering is comparable ).

const uint32_t MAX_SURFACES = 6;
const uint32_t MAIN_RENDER_MASK = 1;

// struct storing the whole information required by CPU and GPU to render a single static object ( trees and buildings )
struct StaticInstanceData
{
  StaticInstanceData(const glm::mat4& p = glm::mat4(), uint32_t t = 0, uint32_t m = 0, float b=1.0f, float wa=0.0f, float wf=1.0f, float wo=0.0f)
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

// struct storing information about dynamic object used during update phase
struct DynamicObjectData
{
  pumex::Kinematic kinematic;
  uint32_t         typeID;
  uint32_t         materialVariant;
  float            time2NextTurn;
  float            brightness;
};

// struct storing the whole information required by GPU to render a single dynamic object
struct DynamicInstanceData
{
  DynamicInstanceData(const glm::mat4& p = glm::mat4(), uint32_t t=0, uint32_t m=0, float b=1.0f)
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

struct UpdateData
{
  UpdateData()
  {
  }
  glm::vec3                                       cameraPosition;
  glm::vec2                                       cameraGeographicCoordinates;
  float                                           cameraDistance;

  std::vector<StaticInstanceData>                 staticInstanceData; // this will only be copied to render data
  std::unordered_map<uint32_t, DynamicObjectData> dynamicObjectData;

  glm::vec2                                       lastMousePos;
  bool                                            leftMouseKeyPressed;
  bool                                            rightMouseKeyPressed;

  bool                                            moveForward;
  bool                                            moveBackward;
  bool                                            moveLeft;
  bool                                            moveRight;
  bool                                            moveUp;
  bool                                            moveDown;
  bool                                            moveFast;
  bool                                            measureTime;
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

  std::vector<StaticInstanceData> staticInstanceData;
  std::vector<DynamicObjectData> dynamicObjectData;
};


// struct that stores information about material used by specific object type. This example does not use textures ( in contrast to crowd example )
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
    float angle = (float)i * glm::two_pi<float>() / (float)propNum;
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

// struct that works as an application database. Render thread uses data from it
// Look at createStaticRendering() and createDynamicRendering() methods to see how to
// register object types, add procedurally created assets and generate object instances
// Look at update() method to see how dynamic objects are updated.
struct GpuCullApplicationData
{
  std::weak_ptr<pumex::Viewer> viewer;
  UpdateData                   updateData;
  std::array<RenderData, 3>    renderData;

  bool  _showStaticRendering  = true;
  bool  _showDynamicRendering = true;
  uint32_t _instancesPerCell  = 4096;
  float _staticAreaSize       = 2000.0f;
  float _dynamicAreaSize      = 1000.0f;
  float _lodModifier          = 1.0f;
  float _densityModifier      = 1.0f;
  float _triangleModifier     = 1.0f;

  std::vector<pumex::VertexSemantic>                   vertexSemantic;
  std::vector<pumex::TextureSemantic>                  textureSemantic;
  std::shared_ptr<pumex::TextureRegistryNull>          textureRegistryNull;

  std::default_random_engine                           randomEngine;

  std::shared_ptr<pumex::DeviceMemoryAllocator>        buffersAllocator;
  std::shared_ptr<pumex::DeviceMemoryAllocator>        verticesAllocator;
  std::shared_ptr<pumex::DeviceMemoryAllocator>        texturesAllocator;


  std::shared_ptr<pumex::AssetBuffer>                  staticAssetBuffer;
  std::shared_ptr<pumex::AssetBufferInstancedResults>  staticInstancedResults;
  std::shared_ptr<pumex::MaterialSet<MaterialGpuCull>> staticMaterialSet;

  std::shared_ptr<pumex::AssetBuffer>                  dynamicAssetBuffer;
  std::shared_ptr<pumex::AssetBufferInstancedResults>  dynamicInstancedResults;
  std::shared_ptr<pumex::MaterialSet<MaterialGpuCull>> dynamicMaterialSet;

  std::shared_ptr<pumex::UniformBufferPerSurface<pumex::Camera>>            cameraUbo;
  std::shared_ptr<pumex::StorageBuffer<StaticInstanceData>>                 staticInstanceSbo;
  std::shared_ptr<pumex::StorageBuffer<DynamicInstanceData>>                dynamicInstanceSbo;

  uint32_t                                                                  blimpID;
  uint32_t                                                                  carID;
  uint32_t                                                                  airplaneID;
  std::map<uint32_t, std::vector<glm::mat4>>                                bonesReset;

  std::exponential_distribution<float>                                      randomTime2NextTurn;
  std::uniform_real_distribution<float>                                     randomRotation;
  std::unordered_map<uint32_t, std::uniform_real_distribution<float>>       randomObjectSpeed;
  uint32_t                                                                 _blimpPropL   = 0;
  uint32_t                                                                 _blimpPropR   = 0;
  uint32_t                                                                 _carWheel0    = 0;
  uint32_t                                                                 _carWheel1    = 0;
  uint32_t                                                                 _carWheel2    = 0;
  uint32_t                                                                 _carWheel3    = 0;
  uint32_t                                                                 _airplaneProp = 0;
  glm::vec2                                                                _minArea;
  glm::vec2                                                                _maxArea;

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

  std::shared_ptr<pumex::UniformBufferPerSurface<pumex::Camera>> textCameraUbo;
  std::shared_ptr<pumex::Font>                         fontDefault;
  std::shared_ptr<pumex::Font>                         fontSmall;
  std::shared_ptr<pumex::Text>                         textDefault;
  std::shared_ptr<pumex::Text>                         textSmall;

  std::shared_ptr<pumex::DescriptorSetLayout>          textDescriptorSetLayout;
  std::shared_ptr<pumex::PipelineLayout>               textPipelineLayout;
  std::shared_ptr<pumex::GraphicsPipeline>             textPipeline;
  std::shared_ptr<pumex::DescriptorPool>               textDescriptorPool;
  std::shared_ptr<pumex::DescriptorSet>                textDescriptorSet;
  std::shared_ptr<pumex::DescriptorSet>                textDescriptorSetSmall;

  std::shared_ptr<pumex::QueryPool>                    timeStampQueryPool;

  pumex::HPClock::time_point                           lastFrameStart;
  bool                                                 measureTime = true;
  std::mutex                                           measureMutex;
  std::unordered_map<uint32_t, double>                 times;

  std::unordered_map<uint32_t, glm::mat4>              slaveViewMatrix;
  std::unordered_map<pumex::Surface*, std::shared_ptr<pumex::CommandBuffer>> myCmdBuffer;

  GpuCullApplicationData(std::shared_ptr<pumex::Viewer> v)
    : viewer{ v }, randomTime2NextTurn { 0.1f }, randomRotation(-glm::pi<float>(), glm::pi<float>())
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
    _minArea = glm::vec2(-0.5f*_dynamicAreaSize, -0.5f*_dynamicAreaSize);
    _maxArea = glm::vec2(0.5f*_dynamicAreaSize, 0.5f*_dynamicAreaSize);
    std::shared_ptr<pumex::Viewer> viewerSh = viewer.lock();
    CHECK_LOG_THROW(viewerSh.get() == nullptr, "Cannot acces pumex viewer");

    // alocate 32 MB for uniform and storage buffers
    buffersAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 32 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 64 MB for vertex and index buffers
    verticesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 64 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 4 MB memory for font textures
    texturesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 4 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);

    vertexSemantic      = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 3 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };
    textureSemantic     = {};
    textureRegistryNull = std::make_shared<pumex::TextureRegistryNull>();

    cameraUbo           = std::make_shared<pumex::UniformBufferPerSurface<pumex::Camera>>(buffersAllocator);
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
    instancedRenderDescriptorPool      = std::make_shared<pumex::DescriptorPool>(3 * MAX_SURFACES, instancedRenderLayoutBindings);
    instancedRenderPipelineLayout      = std::make_shared<pumex::PipelineLayout>();
    instancedRenderPipelineLayout->descriptorSetLayouts.push_back(instancedRenderDescriptorSetLayout);

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

    if (showStaticRendering)
      createStaticRendering();

    if (showDynamicRendering)
      createDynamicRendering();

    std::string fullFontFileName = viewer.lock()->getFullFilePath("fonts/DejaVuSans.ttf");
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
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewer.lock()->getFullFilePath("shaders/text_draw.vert.spv")), "main" },
      { VK_SHADER_STAGE_GEOMETRY_BIT, std::make_shared<pumex::ShaderModule>(viewer.lock()->getFullFilePath("shaders/text_draw.geom.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer.lock()->getFullFilePath("shaders/text_draw.frag.spv")), "main" }
    };
    textPipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    textDescriptorSet = std::make_shared<pumex::DescriptorSet>(textDescriptorSetLayout, textDescriptorPool, 3);
    textDescriptorSet->setSource(0, textCameraUbo);
    textDescriptorSet->setSource(1, fontDefault->fontTexture);

    textDescriptorSetSmall = std::make_shared<pumex::DescriptorSet>(textDescriptorSetLayout, textDescriptorPool, 3);
    textDescriptorSetSmall->setSource(0, textCameraUbo);
    textDescriptorSetSmall->setSource(1, fontSmall->fontTexture);

    timeStampQueryPool = std::make_shared<pumex::QueryPool>(VK_QUERY_TYPE_TIMESTAMP, 8 * MAX_SURFACES);

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

  void createStaticRendering()
  {
    std::shared_ptr<pumex::Viewer> viewerSh = viewer.lock();
    CHECK_LOG_THROW(viewerSh.get() == nullptr, "Cannot acces pumex viewer");

    std::vector<uint32_t> typeIDs;

    std::vector<pumex::AssetBufferVertexSemantics> assetSemantics = { { MAIN_RENDER_MASK, vertexSemantic } };
    staticAssetBuffer      = std::make_shared<pumex::AssetBuffer>(assetSemantics, buffersAllocator, verticesAllocator);
    staticInstancedResults = std::make_shared<pumex::AssetBufferInstancedResults>(assetSemantics, staticAssetBuffer, buffersAllocator);
    staticMaterialSet = std::make_shared<pumex::MaterialSet<MaterialGpuCull>>(viewerSh, textureRegistryNull, buffersAllocator, textureSemantic);

    std::shared_ptr<pumex::Asset> groundAsset(createGround(_staticAreaSize, glm::vec4(0.0f, 0.7f, 0.0f, 1.0f)));
    pumex::BoundingBox groundBbox = pumex::calculateBoundingBox(*groundAsset, MAIN_RENDER_MASK);
    uint32_t groundTypeID = staticAssetBuffer->registerType("ground", pumex::AssetTypeDefinition(groundBbox));
    staticMaterialSet->registerMaterials(groundTypeID, groundAsset);
    staticAssetBuffer->registerObjectLOD(groundTypeID, groundAsset, pumex::AssetLodDefinition(0.0f, 5.0f * _staticAreaSize));
    updateData.staticInstanceData.push_back(StaticInstanceData(glm::mat4(), groundTypeID, 0, 1.0f, 0.0f, 1.0f, 0.0f));

    std::shared_ptr<pumex::Asset> coniferTree0 ( createConiferTree( 0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> coniferTree1 ( createConiferTree(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> coniferTree2 ( createConiferTree(0.15f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox coniferTreeBbox = pumex::calculateBoundingBox(*coniferTree0, MAIN_RENDER_MASK);
    uint32_t coniferTreeID = staticAssetBuffer->registerType("coniferTree", pumex::AssetTypeDefinition(coniferTreeBbox));
    staticMaterialSet->registerMaterials(coniferTreeID, coniferTree0);
    staticMaterialSet->registerMaterials(coniferTreeID, coniferTree1);
    staticMaterialSet->registerMaterials(coniferTreeID, coniferTree2);
    staticAssetBuffer->registerObjectLOD(coniferTreeID, coniferTree0, pumex::AssetLodDefinition(  0.0f * _lodModifier,   100.0f * _lodModifier ));
    staticAssetBuffer->registerObjectLOD(coniferTreeID, coniferTree1, pumex::AssetLodDefinition( 100.0f * _lodModifier,  500.0f * _lodModifier ));
    staticAssetBuffer->registerObjectLOD(coniferTreeID, coniferTree2, pumex::AssetLodDefinition( 500.0f * _lodModifier, 1200.0f * _lodModifier ));
    typeIDs.push_back(coniferTreeID);

    std::shared_ptr<pumex::Asset> decidousTree0 ( createDecidousTree(0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> decidousTree1 ( createDecidousTree(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> decidousTree2 ( createDecidousTree(0.15f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox decidousTreeBbox = pumex::calculateBoundingBox(*decidousTree0, MAIN_RENDER_MASK);
    uint32_t decidousTreeID = staticAssetBuffer->registerType("decidousTree", pumex::AssetTypeDefinition(decidousTreeBbox));
    staticMaterialSet->registerMaterials(decidousTreeID, decidousTree0);
    staticMaterialSet->registerMaterials(decidousTreeID, decidousTree1);
    staticMaterialSet->registerMaterials(decidousTreeID, decidousTree2);
    staticAssetBuffer->registerObjectLOD(decidousTreeID, decidousTree0, pumex::AssetLodDefinition(  0.0f * _lodModifier,   120.0f * _lodModifier ));
    staticAssetBuffer->registerObjectLOD(decidousTreeID, decidousTree1, pumex::AssetLodDefinition( 120.0f * _lodModifier,  600.0f * _lodModifier ));
    staticAssetBuffer->registerObjectLOD(decidousTreeID, decidousTree2, pumex::AssetLodDefinition( 600.0f * _lodModifier, 1400.0f * _lodModifier ));
    typeIDs.push_back(decidousTreeID);

    std::shared_ptr<pumex::Asset> simpleHouse0 ( createSimpleHouse(0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> simpleHouse1 ( createSimpleHouse(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> simpleHouse2 ( createSimpleHouse(0.15f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox simpleHouseBbox = pumex::calculateBoundingBox(*simpleHouse0, MAIN_RENDER_MASK);
    uint32_t simpleHouseID = staticAssetBuffer->registerType("simpleHouse", pumex::AssetTypeDefinition(simpleHouseBbox));
    staticMaterialSet->registerMaterials(simpleHouseID, simpleHouse0);
    staticMaterialSet->registerMaterials(simpleHouseID, simpleHouse1);
    staticMaterialSet->registerMaterials(simpleHouseID, simpleHouse2);
    staticAssetBuffer->registerObjectLOD(simpleHouseID, simpleHouse0, pumex::AssetLodDefinition(0.0f * _lodModifier, 120.0f * _lodModifier));
    staticAssetBuffer->registerObjectLOD(simpleHouseID, simpleHouse1, pumex::AssetLodDefinition(120.0f * _lodModifier, 600.0f * _lodModifier));
    staticAssetBuffer->registerObjectLOD(simpleHouseID, simpleHouse2, pumex::AssetLodDefinition(600.0f * _lodModifier, 1400.0f * _lodModifier));
    typeIDs.push_back(simpleHouseID);

    staticInstancedResults->setup();
    staticMaterialSet->refreshMaterialStructures();

    float objectDensity[3]     = { 10000.0f * _densityModifier, 1000.0f * _densityModifier, 100.0f * _densityModifier };
    float amplitudeModifier[3] = { 1.0f, 1.0f, 0.0f }; // we don't want the house to wave in the wind

    float fullArea = _staticAreaSize * _staticAreaSize;
    std::uniform_real_distribution<float>   randomX(-0.5f*_staticAreaSize, 0.5f * _staticAreaSize);
    std::uniform_real_distribution<float>   randomY(-0.5f*_staticAreaSize, 0.5f * _staticAreaSize);
    std::uniform_real_distribution<float>   randomScale(0.8f, 1.2f);
    std::uniform_real_distribution<float>   randomBrightness(0.5f, 1.0f);
    std::uniform_real_distribution<float>   randomAmplitude(0.01f, 0.05f);
    std::uniform_real_distribution<float>   randomFrequency(0.1f * glm::two_pi<float>(), 0.5f * glm::two_pi<float>());
    std::uniform_real_distribution<float>   randomOffset(0.0f * glm::two_pi<float>(), 1.0f * glm::two_pi<float>());

    for (unsigned int i = 0; i<typeIDs.size(); ++i)
    {
      int objectQuantity = (int)floor(objectDensity[i] * fullArea / 1000000.0f);

      for (int j = 0; j<objectQuantity; ++j)
      {
        glm::vec3 pos( randomX(randomEngine), randomY(randomEngine), 0.0f );
        float rot             = randomRotation(randomEngine);
        float scale           = randomScale(randomEngine);
        float brightness      = randomBrightness(randomEngine);
        float wavingAmplitude = randomAmplitude(randomEngine) * amplitudeModifier[i];
        float wavingFrequency = randomFrequency(randomEngine);
        float wavingOffset    = randomOffset(randomEngine);
        glm::mat4 position(glm::translate(glm::mat4(), glm::vec3(pos.x, pos.y, pos.z)) * glm::rotate(glm::mat4(), rot, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::scale(glm::mat4(), glm::vec3(scale, scale, scale)));
        updateData.staticInstanceData.push_back(StaticInstanceData(position, typeIDs[i], 0, brightness, wavingAmplitude, wavingFrequency, wavingOffset));
      }
    }

    staticInstanceSbo   = std::make_shared<pumex::StorageBuffer<StaticInstanceData>>(buffersAllocator,3);

    staticFilterPipeline = std::make_shared<pumex::ComputePipeline>(pipelineCache, filterPipelineLayout);
    staticFilterPipeline->shaderStage = { VK_SHADER_STAGE_COMPUTE_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/gpucull_static_filter_instances.comp.spv")), "main" };

    staticFilterDescriptorSet = std::make_shared<pumex::DescriptorSet>(filterDescriptorSetLayout, filterDescriptorPool, 3);
    staticFilterDescriptorSet->setSource(0, cameraUbo);
    staticFilterDescriptorSet->setSource(1, staticInstanceSbo);
    staticFilterDescriptorSet->setSource(2, staticAssetBuffer->getTypeBuffer(MAIN_RENDER_MASK));
    staticFilterDescriptorSet->setSource(3, staticAssetBuffer->getLodBuffer(MAIN_RENDER_MASK));
    staticFilterDescriptorSet->setSource(4, staticInstancedResults->getResults(MAIN_RENDER_MASK));
    staticFilterDescriptorSet->setSource(5, staticInstancedResults->getOffsetValues(MAIN_RENDER_MASK));

    staticRenderPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, instancedRenderPipelineLayout, defaultRenderPass, 0);
    staticRenderPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/gpucull_static_render.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/gpucull_static_render.frag.spv")), "main" }
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

    staticRenderDescriptorSet = std::make_shared<pumex::DescriptorSet>(instancedRenderDescriptorSetLayout, instancedRenderDescriptorPool, 3);
    staticRenderDescriptorSet->setSource(0, cameraUbo);
    staticRenderDescriptorSet->setSource(1, staticInstanceSbo);
    staticRenderDescriptorSet->setSource(2, staticInstancedResults->getOffsetValues(MAIN_RENDER_MASK));
    staticRenderDescriptorSet->setSource(3, staticMaterialSet->typeDefinitionSbo);
    staticRenderDescriptorSet->setSource(4, staticMaterialSet->materialVariantSbo);
    staticRenderDescriptorSet->setSource(5, staticMaterialSet->materialDefinitionSbo);
  }

  void createDynamicRendering()
  {
    std::shared_ptr<pumex::Viewer> viewerSh = viewer.lock();
    CHECK_LOG_THROW(viewerSh.get() == nullptr, "Cannot acces pumex viewer");

    std::vector<uint32_t> typeIDs;

    std::vector<pumex::AssetBufferVertexSemantics> assetSemantics = { { 1, vertexSemantic } };
    dynamicAssetBuffer = std::make_shared<pumex::AssetBuffer>(assetSemantics, buffersAllocator, verticesAllocator);
    dynamicInstancedResults = std::make_shared<pumex::AssetBufferInstancedResults>(assetSemantics, dynamicAssetBuffer, buffersAllocator);
    dynamicMaterialSet = std::make_shared<pumex::MaterialSet<MaterialGpuCull>>(viewerSh, textureRegistryNull, buffersAllocator, textureSemantic);

    std::shared_ptr<pumex::Asset> blimpLod0 ( createBlimp(0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)) );
    std::shared_ptr<pumex::Asset> blimpLod1 ( createBlimp(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)) );
    std::shared_ptr<pumex::Asset> blimpLod2 ( createBlimp(0.20f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)) );
    pumex::BoundingBox blimpBbox = pumex::calculateBoundingBox(*blimpLod0, MAIN_RENDER_MASK);
    blimpID = dynamicAssetBuffer->registerType("blimp", pumex::AssetTypeDefinition(blimpBbox));
    dynamicMaterialSet->registerMaterials(blimpID, blimpLod0);
    dynamicMaterialSet->registerMaterials(blimpID, blimpLod1);
    dynamicMaterialSet->registerMaterials(blimpID, blimpLod2);
    dynamicAssetBuffer->registerObjectLOD(blimpID, blimpLod0, pumex::AssetLodDefinition(0.0f * _lodModifier, 150.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(blimpID, blimpLod1, pumex::AssetLodDefinition(150.0f * _lodModifier, 800.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(blimpID, blimpLod2, pumex::AssetLodDefinition(800.0f * _lodModifier, 6500.0f * _lodModifier));
    typeIDs.push_back(blimpID);
    _blimpPropL = blimpLod0->skeleton.invBoneNames["propL"];
    _blimpPropR = blimpLod0->skeleton.invBoneNames["propR"];
    bonesReset[blimpID] = pumex::calculateResetPosition(*blimpLod0);

    std::shared_ptr<pumex::Asset> carLod0(createCar(0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.3, 0.3, 0.3, 1.0)));
    std::shared_ptr<pumex::Asset> carLod1(createCar(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> carLod2(createCar(0.15f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox carBbox = pumex::calculateBoundingBox(*carLod0, MAIN_RENDER_MASK);
    carID = dynamicAssetBuffer->registerType("car", pumex::AssetTypeDefinition(carBbox));
    dynamicMaterialSet->registerMaterials(carID, carLod0);
    dynamicMaterialSet->registerMaterials(carID, carLod1);
    dynamicMaterialSet->registerMaterials(carID, carLod2);
    dynamicAssetBuffer->registerObjectLOD(carID, carLod0, pumex::AssetLodDefinition(0.0f * _lodModifier, 50.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(carID, carLod1, pumex::AssetLodDefinition(50.0f * _lodModifier, 300.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(carID, carLod2, pumex::AssetLodDefinition(300.0f * _lodModifier, 1000.0f * _lodModifier));
    typeIDs.push_back(carID);
    _carWheel0 = carLod0->skeleton.invBoneNames["wheel0"];
    _carWheel1 = carLod0->skeleton.invBoneNames["wheel1"];
    _carWheel2 = carLod0->skeleton.invBoneNames["wheel2"];
    _carWheel3 = carLod0->skeleton.invBoneNames["wheel3"];

    bonesReset[carID] = pumex::calculateResetPosition(*carLod0);

    std::shared_ptr<pumex::Asset> airplaneLod0(createAirplane(0.75f * _triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> airplaneLod1(createAirplane(0.45f * _triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> airplaneLod2(createAirplane(0.15f * _triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox airplaneBbox = pumex::calculateBoundingBox(*airplaneLod0, MAIN_RENDER_MASK);
    airplaneID = dynamicAssetBuffer->registerType("airplane", pumex::AssetTypeDefinition(airplaneBbox));
    dynamicMaterialSet->registerMaterials(airplaneID, airplaneLod0);
    dynamicMaterialSet->registerMaterials(airplaneID, airplaneLod1);
    dynamicMaterialSet->registerMaterials(airplaneID, airplaneLod2);
    dynamicAssetBuffer->registerObjectLOD(airplaneID, airplaneLod0, pumex::AssetLodDefinition(0.0f * _lodModifier, 80.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(airplaneID, airplaneLod1, pumex::AssetLodDefinition(80.0f * _lodModifier, 400.0f * _lodModifier));
    dynamicAssetBuffer->registerObjectLOD(airplaneID, airplaneLod2, pumex::AssetLodDefinition(400.0f * _lodModifier, 1200.0f * _lodModifier));
    typeIDs.push_back(airplaneID);
    _airplaneProp = airplaneLod0->skeleton.invBoneNames["prop"];
    bonesReset[airplaneID] = pumex::calculateResetPosition(*airplaneLod0);

    dynamicInstancedResults->setup();
    dynamicMaterialSet->refreshMaterialStructures();

    float objectZ[3]        = { 50.0f, 0.0f, 25.0f };
    float objectDensity[3]  = { 100.0f * _densityModifier, 100.0f * _densityModifier, 100.0f * _densityModifier };
    float minObjectSpeed[3] = { 5.0f, 1.0f, 10.0f };
    float maxObjectSpeed[3] = { 10.0f, 5.0f, 16.0f };

    for (uint32_t i = 0; i<typeIDs.size(); ++i)
      randomObjectSpeed.insert({ typeIDs[i],std::uniform_real_distribution<float>(minObjectSpeed[i], maxObjectSpeed[i]) });

    float fullArea = _dynamicAreaSize * _dynamicAreaSize;
    std::uniform_real_distribution<float>              randomX(_minArea.x, _maxArea.x);
    std::uniform_real_distribution<float>              randomY(_minArea.y, _maxArea.y);
    std::uniform_real_distribution<float>              randomBrightness(0.5f, 1.0f);

    uint32_t objectID = 0;
    for (uint32_t i = 0; i<typeIDs.size(); ++i)
    {

      int objectQuantity = (int)floor(objectDensity[i] * fullArea / 1000000.0f);
      for (int j = 0; j<objectQuantity; ++j)
      {
        objectID++;
        DynamicObjectData objectData;
        objectData.typeID                = typeIDs[i];
        objectData.kinematic.position    = glm::vec3(randomX(randomEngine), randomY(randomEngine), objectZ[i]);
        objectData.kinematic.orientation = glm::angleAxis(randomRotation(randomEngine), glm::vec3(0.0f, 0.0f, 1.0f));
        objectData.kinematic.velocity    = glm::rotate(objectData.kinematic.orientation, glm::vec3(1, 0, 0)) * randomObjectSpeed[objectData.typeID](randomEngine);
        objectData.brightness            = randomBrightness(randomEngine);
        objectData.time2NextTurn         = randomTime2NextTurn(randomEngine);

        //glm::mat4 position(glm::translate(glm::mat4(), glm::vec3(pos.x, pos.y, pos.z)) * glm::rotate(glm::mat4(), rot, glm::vec3(0.0f, 0.0f, 1.0f)));
        //DynamicInstanceDataCPU instanceDataCPU(pos, rot, speed, time2NextTurn);
        //DynamicInstanceData instanceData(position, typeIDs[i], 0, brightness);
        //for (uint32_t k = 0; k<bonesReset[typeIDs[i]].size() && k<MAX_BONES; ++k)
        //  instanceData.bones[k] = bonesReset[typeIDs[i]][k];
        updateData.dynamicObjectData.insert({ objectID,objectData });
      }
    }

    dynamicInstanceSbo  = std::make_shared<pumex::StorageBuffer<DynamicInstanceData>>(buffersAllocator,3);

    dynamicFilterPipeline = std::make_shared<pumex::ComputePipeline>(pipelineCache, filterPipelineLayout);
    dynamicFilterPipeline->shaderStage = { VK_SHADER_STAGE_COMPUTE_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/gpucull_dynamic_filter_instances.comp.spv")), "main" };

    dynamicFilterDescriptorSet = std::make_shared<pumex::DescriptorSet>(filterDescriptorSetLayout, filterDescriptorPool, 3 );
    dynamicFilterDescriptorSet->setSource(0, cameraUbo);
    dynamicFilterDescriptorSet->setSource(1, dynamicInstanceSbo);
    dynamicFilterDescriptorSet->setSource(2, dynamicAssetBuffer->getTypeBuffer(MAIN_RENDER_MASK));
    dynamicFilterDescriptorSet->setSource(3, dynamicAssetBuffer->getLodBuffer(MAIN_RENDER_MASK));
    dynamicFilterDescriptorSet->setSource(4, dynamicInstancedResults->getResults(MAIN_RENDER_MASK));
    dynamicFilterDescriptorSet->setSource(5, dynamicInstancedResults->getOffsetValues(MAIN_RENDER_MASK));

    dynamicRenderPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, instancedRenderPipelineLayout, defaultRenderPass, 0);
    dynamicRenderPipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/gpucull_dynamic_render.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewerSh->getFullFilePath("shaders/gpucull_dynamic_render.frag.spv")), "main" }
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

    dynamicRenderDescriptorSet = std::make_shared<pumex::DescriptorSet>(instancedRenderDescriptorSetLayout, instancedRenderDescriptorPool, 3);
    dynamicRenderDescriptorSet->setSource(0, cameraUbo);
    dynamicRenderDescriptorSet->setSource(1, dynamicInstanceSbo);
    dynamicRenderDescriptorSet->setSource(2, dynamicInstancedResults->getOffsetValues(MAIN_RENDER_MASK));
    dynamicRenderDescriptorSet->setSource(3, dynamicMaterialSet->typeDefinitionSbo);
    dynamicRenderDescriptorSet->setSource(4, dynamicMaterialSet->materialVariantSbo);
    dynamicRenderDescriptorSet->setSource(5, dynamicMaterialSet->materialDefinitionSbo);
  }

  void surfaceSetup(std::shared_ptr<pumex::Surface> surface)
  {
    pumex::Device*      devicePtr      = surface->device.lock().get();
    pumex::CommandPool* commandPoolPtr = surface->commandPool.get();

    myCmdBuffer[surface.get()] = std::make_shared<pumex::CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, devicePtr, commandPoolPtr, surface->getImageCount());

    pipelineCache->validate(devicePtr);
    instancedRenderDescriptorSetLayout->validate(devicePtr);
    instancedRenderDescriptorPool->validate(devicePtr);
    instancedRenderPipelineLayout->validate(devicePtr);
    filterDescriptorSetLayout->validate(devicePtr);
    filterDescriptorPool->validate(devicePtr);
    filterPipelineLayout->validate(devicePtr);

    textDescriptorSetLayout->validate(devicePtr);
    textDescriptorPool->validate(devicePtr);
    textPipelineLayout->validate(devicePtr);
    textPipeline->validate(devicePtr);

    timeStampQueryPool->validate(surface.get());

    if (_showStaticRendering)
    {
      staticAssetBuffer->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
      staticInstancedResults->validate(surface.get());
      staticMaterialSet->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
      staticRenderPipeline->validate(devicePtr);
      staticFilterPipeline->validate(devicePtr);
    }

    if (_showDynamicRendering)
    {
      dynamicAssetBuffer->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
      dynamicInstancedResults->validate(surface.get());
      dynamicMaterialSet->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
      dynamicRenderPipeline->validate(devicePtr);
      dynamicFilterPipeline->validate(devicePtr);
    }
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
        case pumex::InputEvent::W:     updateData.moveForward  = true; break;
        case pumex::InputEvent::S:     updateData.moveBackward = true; break;
        case pumex::InputEvent::A:     updateData.moveLeft     = true; break;
        case pumex::InputEvent::D:     updateData.moveRight    = true; break;
        case pumex::InputEvent::Q:     updateData.moveUp       = true; break;
        case pumex::InputEvent::Z:     updateData.moveDown     = true; break;
        case pumex::InputEvent::SHIFT: updateData.moveFast     = true; break;
        case pumex::InputEvent::T:     updateData.measureTime  = !updateData.measureTime; break;
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

    float camSpeed = 1.0f;
    if (updateData.moveFast)
      camSpeed = 5.0f;
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
    uData.cameraDistance = updateData.cameraDistance;
    uData.cameraPosition = updateData.cameraPosition;
  }

  void update(float timeSinceStart, float updateStep)
  {
    // send UpdateData to RenderData
    uint32_t updateIndex = viewer.lock()->getUpdateIndex();

    if (_showStaticRendering)
    {
      // no modifications to static data - just copy it to render data
      renderData[updateIndex].staticInstanceData = updateData.staticInstanceData;
    }
    if (_showDynamicRendering)
    {
      std::vector< std::unordered_map<uint32_t, DynamicObjectData>::iterator > iters;
      for (auto it = updateData.dynamicObjectData.begin(); it != updateData.dynamicObjectData.end(); ++it)
        iters.push_back(it);
      tbb::parallel_for
      (
        tbb::blocked_range<size_t>(0, iters.size()),
        [=](const tbb::blocked_range<size_t>& r)
        {
          for (size_t i = r.begin(); i != r.end(); ++i)
            updateInstance(iters[i]->second, timeSinceStart, updateStep);
        }
      );

      renderData[updateIndex].dynamicObjectData.resize(0);
      for (auto it = updateData.dynamicObjectData.begin(); it != updateData.dynamicObjectData.end(); ++it)
        renderData[updateIndex].dynamicObjectData.push_back(it->second);

    }
  }

  void updateInstance(DynamicObjectData& objectData, float timeSinceStart, float updateStep)
  {
    if (objectData.time2NextTurn < 0.0f)
    {
      objectData.kinematic.orientation = glm::angleAxis(randomRotation(randomEngine), glm::vec3(0.0f, 0.0f, 1.0f));
      objectData.kinematic.velocity = glm::rotate(objectData.kinematic.orientation, glm::vec3(1, 0, 0)) * randomObjectSpeed[objectData.typeID](randomEngine);
      objectData.time2NextTurn = randomTime2NextTurn(randomEngine);
    }
    else
      objectData.time2NextTurn -= updateStep;

    // calculate new position
    objectData.kinematic.position += objectData.kinematic.velocity * updateStep;

    // change direction if bot is leaving designated area
    bool isOutside[] =
    {
      objectData.kinematic.position.x < _minArea.x ,
      objectData.kinematic.position.x > _maxArea.x ,
      objectData.kinematic.position.y < _minArea.y ,
      objectData.kinematic.position.y > _maxArea.y
    };
    if (isOutside[0] || isOutside[1] || isOutside[2] || isOutside[3])
    {
      objectData.kinematic.position.x = std::max(objectData.kinematic.position.x, _minArea.x);
      objectData.kinematic.position.x = std::min(objectData.kinematic.position.x, _maxArea.x);
      objectData.kinematic.position.y = std::max(objectData.kinematic.position.y, _minArea.y);
      objectData.kinematic.position.y = std::min(objectData.kinematic.position.y, _maxArea.y);

      glm::vec4 direction = objectData.kinematic.orientation *  glm::vec4(1, 0, 0, 1);
      if (isOutside[0] || isOutside[1])
        direction.x *= -1.0f;
      if (isOutside[2] || isOutside[3])
        direction.y *= -1.0f;

      objectData.kinematic.orientation = glm::angleAxis(atan2f(direction.y, direction.x), glm::vec3(0.0f, 0.0f, 1.0f));
      objectData.kinematic.velocity    = glm::rotate(objectData.kinematic.orientation, glm::vec3(1, 0, 0)) * randomObjectSpeed[objectData.typeID](randomEngine);
      objectData.time2NextTurn         = randomTime2NextTurn(randomEngine);
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
    camera.setProjectionMatrix(glm::perspective(glm::radians(60.0f), (float)renderWidth / (float)renderHeight, 0.1f, 100000.0f));
    cameraUbo->set(surface.get(), camera);

    pumex::Camera textCamera;
    textCamera.setProjectionMatrix(glm::ortho(0.0f, (float)renderWidth, 0.0f, (float)renderHeight), false);
    textCameraUbo->set(surface.get(), textCamera);
  }

  void prepareStaticBuffersForRendering()
  {
    uint32_t renderIndex = viewer.lock()->getRenderIndex();
    const RenderData& rData = renderData[renderIndex];

    // Warning: if you want to change quantity and types of rendered objects then you have to recalculate instance offsets
    staticInstanceSbo->set(rData.staticInstanceData);

    std::vector<uint32_t> typeCount(staticAssetBuffer->getNumTypesID());
    std::fill(typeCount.begin(), typeCount.end(), 0);

    // compute how many instances of each type there is
    for (uint32_t i = 0; i<rData.staticInstanceData.size(); ++i)
      typeCount[rData.staticInstanceData[i].typeID]++;

    staticInstancedResults->prepareBuffers(typeCount);
  }

  void prepareDynamicBuffersForRendering()
  {
    uint32_t renderIndex = viewer.lock()->getRenderIndex();
    const RenderData& rData = renderData[renderIndex];

    float deltaTime = pumex::inSeconds(viewer.lock()->getRenderTimeDelta());
    float renderTime = pumex::inSeconds(viewer.lock()->getUpdateTime() - viewer.lock()->getApplicationStartTime()) + deltaTime;

    std::vector<uint32_t> typeCount(dynamicAssetBuffer->getNumTypesID());
    std::fill(typeCount.begin(), typeCount.end(), 0);

    // compute how many instances of each type there is
    for (uint32_t i = 0; i<rData.dynamicObjectData.size(); ++i)
      typeCount[rData.dynamicObjectData[i].typeID]++;

    dynamicInstancedResults->prepareBuffers(typeCount);

    std::vector<DynamicInstanceData> dynamicInstanceData;
    for (auto it = rData.dynamicObjectData.begin(); it != rData.dynamicObjectData.end(); ++it)
    {
      DynamicInstanceData diData(pumex::extrapolate(it->kinematic, deltaTime), it->typeID, it->materialVariant, it->brightness);

      float speed = glm::length(it->kinematic.velocity);
      // calculate new positions for wheels and propellers
      if (diData.typeID == blimpID)
      {
        diData.bones[_blimpPropL] = bonesReset[diData.typeID][_blimpPropL] * glm::rotate(glm::mat4(), fmodf(glm::two_pi<float>() *  0.5f * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
        diData.bones[_blimpPropR] = bonesReset[diData.typeID][_blimpPropR] * glm::rotate(glm::mat4(), fmodf(glm::two_pi<float>() * -0.5f * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
      }
      if (diData.typeID == carID)
      {
        diData.bones[_carWheel0] = bonesReset[diData.typeID][_carWheel0] * glm::rotate(glm::mat4(), fmodf(( speed / 0.5f) * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
        diData.bones[_carWheel1] = bonesReset[diData.typeID][_carWheel1] * glm::rotate(glm::mat4(), fmodf(( speed / 0.5f) * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
        diData.bones[_carWheel2] = bonesReset[diData.typeID][_carWheel2] * glm::rotate(glm::mat4(), fmodf((-speed / 0.5f) * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
        diData.bones[_carWheel3] = bonesReset[diData.typeID][_carWheel3] * glm::rotate(glm::mat4(), fmodf((-speed / 0.5f) * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
      }
      if (diData.typeID == airplaneID)
      {
        diData.bones[_airplaneProp] = bonesReset[diData.typeID][_airplaneProp] * glm::rotate(glm::mat4(), fmodf(glm::two_pi<float>() *  -1.5f * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
      }
      dynamicInstanceData.emplace_back(diData);
    }
    dynamicInstanceSbo->set(dynamicInstanceData);
  }

  void draw(std::shared_ptr<pumex::Surface> surface)
  {
    pumex::Surface*     surfacePtr = surface.get();
    pumex::Device*      devicePtr = surface->device.lock().get();
    pumex::CommandPool* commandPoolPtr = surface->commandPool.get();
    uint32_t            renderIndex = surface->viewer.lock()->getRenderIndex();
    const RenderData&   rData = renderData[renderIndex];
    unsigned long long  frameNumber = surface->viewer.lock()->getFrameNumber();
    uint32_t            activeIndex = frameNumber % 3;
    uint32_t            renderWidth = surface->swapChainSize.width;
    uint32_t            renderHeight = surface->swapChainSize.height;

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
      stream << "Prepare static buffers    : " << std::fixed << std::setprecision(3) << 1000.0 * times[2010] << " ms";
      textSmall->setText(surfacePtr, 2010, glm::vec2(30, 118), glm::vec4(0.9f, 0.0f, 0.0f, 1.0f), stream.str());

      stream.str(L"");
      stream << "Prepare dynamic buffers    : " << std::fixed << std::setprecision(3) << 1000.0 * times[2011] << " ms";
      textSmall->setText(surfacePtr, 2011, glm::vec2(30, 138), glm::vec4(0.9f, 0.0f, 0.0f, 1.0f), stream.str());

      stream.str(L"");
      stream << "Begin frame            : " << std::fixed << std::setprecision(3) << 1000.0 * times[2020+surfacePtr->getID()] << " ms";
      textSmall->setText(surfacePtr, 2020, glm::vec2(30, 158), glm::vec4(0.9f, 0.0f, 0.0f, 1.0f), stream.str());

      stream.str(L"");
      stream << "Draw frame             : " << std::fixed << std::setprecision(3) << 1000.0 * times[2030 + surfacePtr->getID()] << " ms";
      textSmall->setText(surfacePtr, 2030, glm::vec2(30, 178), glm::vec4(0.9f, 0.0f, 0.0f, 1.0f), stream.str());

      stream.str(L"");
      stream << "End frame               : " << std::fixed << std::setprecision(3) << 1000.0 * times[2040 + surfacePtr->getID()] << " ms";
      textSmall->setText(surfacePtr, 2040, glm::vec2(30, 198), glm::vec4(0.9f, 0.0f, 0.0f, 1.0f), stream.str());

      float timeStampPeriod = devicePtr->physical.lock()->properties.limits.timestampPeriod / 1000000.0f;
      std::vector<uint64_t> queryResults;
      // We use swapChainImageIndex to get the time measurments from previous frame - timeStampQueryPool works like circular buffer
      queryResults = timeStampQueryPool->getResults(surfacePtr, ((activeIndex + 2) % 3) * 8, 8, 0);
      uint32_t y = 238;
      if (_showStaticRendering)
      {
        // exclude timer overflows
        if (queryResults[1] > queryResults[0])
        {
          stream.str(L"");
          stream << "GPU LOD compute static : " << std::fixed << std::setprecision(3) << (queryResults[1] - queryResults[0]) * timeStampPeriod << " ms";
          textSmall->setText(surfacePtr, 3010, glm::vec2(30, y), glm::vec4(0.8f, 0.8f, 0.0f, 1.0f), stream.str());
        }
        y += 20;

        // exclude timer overflows
        if (queryResults[5] > queryResults[4])
        {
          stream.str(L"");
          stream << "GPU draw shader static       : " << std::fixed << std::setprecision(3) << (queryResults[5] - queryResults[4]) * timeStampPeriod << " ms";
          textSmall->setText(surfacePtr, 3020, glm::vec2(30, y), glm::vec4(0.8f, 0.8f, 0.0f, 1.0f), stream.str());
        }
        y += 20;
      }
      if (_showDynamicRendering)
      {
        // exclude timer overflows
        if (queryResults[3] > queryResults[2])
        {
          stream.str(L"");
          stream << "GPU LOD compute dynamic : " << std::fixed << std::setprecision(3) << (queryResults[3] - queryResults[2]) * timeStampPeriod << " ms";
          textSmall->setText(surfacePtr, 3030, glm::vec2(30, y), glm::vec4(0.8f, 0.8f, 0.0f, 1.0f), stream.str());
        }
        y += 20;

        // exclude timer overflows
        if (queryResults[7] > queryResults[6])
        {
          stream.str(L"");
          stream << "GPU draw shader dynamic     : " << std::fixed << std::setprecision(3) << (queryResults[7] - queryResults[6]) * timeStampPeriod << " ms";
          textSmall->setText(surfacePtr, 3040, glm::vec2(30, y), glm::vec4(0.8f, 0.8f, 0.0f, 1.0f), stream.str());
        }
        y += 20;
      }
    }
    textSmall->setActiveIndex(activeIndex);
    textSmall->validate(surfacePtr);


    cameraUbo->validate(surfacePtr);

    if (_showStaticRendering)
    {
      staticInstanceSbo->setActiveIndex(activeIndex);
      staticInstanceSbo->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
      staticInstancedResults->setActiveIndex(activeIndex);
      staticInstancedResults->validate(surfacePtr);

      staticRenderDescriptorSet->setActiveIndex(activeIndex);
      staticRenderDescriptorSet->validate(surfacePtr);
      staticFilterDescriptorSet->setActiveIndex(activeIndex);
      staticFilterDescriptorSet->validate(surfacePtr);
    }

    if (_showDynamicRendering)
    {
      dynamicInstanceSbo->setActiveIndex(activeIndex);
      dynamicInstanceSbo->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
      dynamicInstancedResults->setActiveIndex(activeIndex);
      dynamicInstancedResults->validate(surfacePtr);

      dynamicRenderDescriptorSet->setActiveIndex(activeIndex);
      dynamicRenderDescriptorSet->validate(surfacePtr);
      dynamicFilterDescriptorSet->setActiveIndex(activeIndex);
      dynamicFilterDescriptorSet->validate(surfacePtr);
    }
    textCameraUbo->validate(surfacePtr);
    textDescriptorSet->setActiveIndex(activeIndex);
    textDescriptorSet->validate(surfacePtr);

    textDescriptorSetSmall->setActiveIndex(activeIndex);
    textDescriptorSetSmall->validate(surfacePtr);

    auto currentCmdBuffer = myCmdBuffer[surfacePtr];
    currentCmdBuffer->setActiveIndex(activeIndex);
    if (currentCmdBuffer->isDirty(activeIndex))
    {
      currentCmdBuffer->cmdBegin();

      timeStampQueryPool->reset(surfacePtr, currentCmdBuffer, activeIndex * 8, 8);

      std::vector<pumex::DescriptorSetValue> staticResultsBuffer, dynamicResultsBuffer;
      uint32_t staticDrawCount, dynamicDrawCount;

      // Set up memory barrier to ensure that the indirect commands have been consumed before the compute shaders update them
      if (_showStaticRendering)
      {
        staticInstancedResults->getResults(MAIN_RENDER_MASK)->getDescriptorSetValues(surface->surface, activeIndex, staticResultsBuffer);
        staticDrawCount = staticInstancedResults->getDrawCount(MAIN_RENDER_MASK);
      }
      if (_showDynamicRendering)
      {
        dynamicInstancedResults->getResults(MAIN_RENDER_MASK)->getDescriptorSetValues(surface->surface, activeIndex, dynamicResultsBuffer);
        dynamicDrawCount = dynamicInstancedResults->getDrawCount(MAIN_RENDER_MASK);
      }

      // perform compute shaders
      if (_showStaticRendering)
      {
        if (measureTime)
          timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 8 + 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        currentCmdBuffer->cmdBindPipeline(staticFilterPipeline.get());
        currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, surfacePtr, filterPipelineLayout.get(), 0, staticFilterDescriptorSet.get());
        currentCmdBuffer->cmdDispatch(rData.staticInstanceData.size() / 16 + ((rData.staticInstanceData.size() % 16 > 0) ? 1 : 0), 1, 1);
        if (measureTime)
          timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 8 + 1, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
      }
      if (_showDynamicRendering)
      {
        if (measureTime)
          timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 8 + 2, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        currentCmdBuffer->cmdBindPipeline(dynamicFilterPipeline.get());
        currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, surfacePtr, filterPipelineLayout.get(), 0, dynamicFilterDescriptorSet.get());
        currentCmdBuffer->cmdDispatch(rData.dynamicObjectData.size() / 16 + ((rData.dynamicObjectData.size() % 16 > 0) ? 1 : 0), 1, 1);
        if (measureTime)
          timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 8 + 3, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
      }

      // setup memory barriers, so that copying data to *resultsSbo2 will start only after compute shaders finish working
      std::vector<pumex::PipelineBarrier> afterComputeBarriers;
      if (_showStaticRendering)
        afterComputeBarriers.emplace_back(pumex::PipelineBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, surface->presentationQueueFamilyIndex, surface->presentationQueueFamilyIndex, staticResultsBuffer[0].bufferInfo));
      if (_showDynamicRendering)
        afterComputeBarriers.emplace_back(pumex::PipelineBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, surface->presentationQueueFamilyIndex, surface->presentationQueueFamilyIndex, dynamicResultsBuffer[0].bufferInfo));
      if(!afterComputeBarriers.empty())
        currentCmdBuffer->cmdPipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, afterComputeBarriers);

      std::vector<VkClearValue> clearValues = { pumex::makeColorClearValue(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), pumex::makeDepthStencilClearValue(1.0f, 0) };
      currentCmdBuffer->cmdBeginRenderPass(surfacePtr, defaultRenderPass.get(), surface->frameBuffer.get(), surface->getImageIndex(), pumex::makeVkRect2D(0, 0, renderWidth, renderHeight), clearValues);
      currentCmdBuffer->cmdSetViewport(0, { pumex::makeViewport(0, 0, renderWidth, renderHeight, 0.0f, 1.0f) });
      currentCmdBuffer->cmdSetScissor(0, { pumex::makeVkRect2D(0, 0, renderWidth, renderHeight) });

      if (_showStaticRendering)
      {
        if (measureTime)
          timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 8 + 4, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        currentCmdBuffer->cmdBindPipeline(staticRenderPipeline.get());
        currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePtr, instancedRenderPipelineLayout.get(), 0, staticRenderDescriptorSet.get());
        staticAssetBuffer->cmdBindVertexIndexBuffer(devicePtr, currentCmdBuffer.get(), 1, 0);
        if (devicePtr->physical.lock()->features.multiDrawIndirect == 1)
          currentCmdBuffer->cmdDrawIndexedIndirect(staticResultsBuffer[0].bufferInfo.buffer, staticResultsBuffer[0].bufferInfo.offset, staticDrawCount, sizeof(pumex::DrawIndexedIndirectCommand));
        else
        {
          for (uint32_t i = 0; i < staticDrawCount; ++i)
            currentCmdBuffer->cmdDrawIndexedIndirect(staticResultsBuffer[0].bufferInfo.buffer, staticResultsBuffer[0].bufferInfo.offset + i * sizeof(pumex::DrawIndexedIndirectCommand), 1, sizeof(pumex::DrawIndexedIndirectCommand));
        }
        if (measureTime)
          timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 8 + 5, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
      }
      if (_showDynamicRendering)
      {
        if (measureTime)
          timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 8 + 6, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        currentCmdBuffer->cmdBindPipeline(dynamicRenderPipeline.get());
        currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePtr, instancedRenderPipelineLayout.get(), 0, dynamicRenderDescriptorSet.get());
        dynamicAssetBuffer->cmdBindVertexIndexBuffer(devicePtr, currentCmdBuffer.get(), 1, 0);
        if (devicePtr->physical.lock()->features.multiDrawIndirect == 1)
          currentCmdBuffer->cmdDrawIndexedIndirect(dynamicResultsBuffer[0].bufferInfo.buffer, dynamicResultsBuffer[0].bufferInfo.offset, dynamicDrawCount, sizeof(pumex::DrawIndexedIndirectCommand));
        else
        {
          for (uint32_t i = 0; i < dynamicDrawCount; ++i)
            currentCmdBuffer->cmdDrawIndexedIndirect(dynamicResultsBuffer[0].bufferInfo.buffer, dynamicResultsBuffer[0].bufferInfo.offset + i * sizeof(pumex::DrawIndexedIndirectCommand), 1, sizeof(pumex::DrawIndexedIndirectCommand));
        }
        if (measureTime)
          timeStampQueryPool->queryTimeStamp(surfacePtr, currentCmdBuffer, activeIndex * 8 + 7, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
      }

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

  void fillFPS()
  {
    pumex::HPClock::time_point thisFrameStart = pumex::HPClock::now();
    double fpsValue = 1.0 / pumex::inSeconds(thisFrameStart - lastFrameStart);
    lastFrameStart = thisFrameStart;

    std::wstringstream stream;
    stream << "FPS : " << std::fixed << std::setprecision(1) << fpsValue;
    textDefault->setText(viewer.lock()->getSurface(0), 0, glm::vec2(30, 28), glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), stream.str());
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

// thread that renders data to a Vulkan surface

int main(int argc, char * argv[])
{
  SET_LOG_INFO;
  args::ArgumentParser    parser("pumex example : instanced rendering for static and dynamic objects");
  args::HelpFlag          help(parser, "help", "display this help menu", { 'h', "help" });
  args::Flag              enableDebugging(parser, "debug", "enable Vulkan debugging", { 'd' });
  args::Flag              useFullScreen(parser, "fullscreen", "create fullscreen window", { 'f' });
  args::Flag              renderVRwindows(parser, "vrwindows", "create two halfscreen windows for VR", { 'v' });
  args::Flag              render3windows(parser, "three_windows", "render in three windows", { 't' });
  args::Flag              skipStaticRendering(parser, "skip-static", "skip rendering of static objects", { "skip-static" });
  args::Flag              skipDynamicRendering(parser, "skip-dynamic", "skip rendering of dynamic objects", { "skip-dynamic" });
  args::ValueFlag<float>  staticAreaSizeArg(parser, "static-area-size", "size of the area for static rendering", { "static-area-size" }, 2000.0f);
  args::ValueFlag<float>  dynamicAreaSizeArg(parser, "dynamic-area-size", "size of the area for dynamic rendering", { "dynamic-area-size" }, 1000.0f);
  args::ValueFlag<float>  lodModifierArg(parser, "lod-modifier", "LOD range [%]", { "lod-modifier" }, 100.0f);
  args::ValueFlag<float>  densityModifierArg(parser, "density-modifier", "instance density [%]", { "density-modifier" }, 100.0f);
  args::ValueFlag<float>  triangleModifierArg(parser, "triangle-modifier", "instance triangle quantity [%]", { "triangle-modifier" }, 100.0f);
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

  bool  showStaticRendering  = !skipStaticRendering;
  bool  showDynamicRendering = !skipDynamicRendering;
  float staticAreaSize       = args::get(staticAreaSizeArg);
  float dynamicAreaSize      = args::get(dynamicAreaSizeArg);
  float lodModifier          = args::get(lodModifierArg) / 100.0f;      // lod distances are multiplied by this parameter
  float densityModifier      = args::get(densityModifierArg) / 100.0f;  // density of objects is multiplied by this parameter
  float triangleModifier     = args::get(triangleModifierArg) / 100.0f; // the number of triangles on geometries is multiplied by this parameter
  LOG_INFO << "Object culling on GPU";
  if (enableDebugging)
    LOG_INFO << " : Vulkan debugging enabled";
  LOG_INFO << std::endl;


  // Below is the definition of Vulkan instance, devices, queues, surfaces, windows, render passes and render threads. All in one place - with all parameters listed
  const std::vector<std::string> requestDebugLayers = { { "VK_LAYER_LUNARG_standard_validation" } };
  pumex::ViewerTraits viewerTraits{ "Gpu cull comparison", enableDebugging, requestDebugLayers, 60 };
  viewerTraits.debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT;// | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;

  std::shared_ptr<pumex::Viewer> viewer;
  try
  {
    viewer = std::make_shared<pumex::Viewer>(viewerTraits);

    std::vector<pumex::WindowTraits> windowTraits;
    if (render3windows)
    {
      windowTraits.emplace_back(pumex::WindowTraits{ 0, 30,   100, 512, 384, pumex::WindowTraits::WINDOW, "Object culling on GPU 1" });
      windowTraits.emplace_back(pumex::WindowTraits{ 0, 570,  100, 512, 384, pumex::WindowTraits::WINDOW, "Object culling on GPU 2" });
      windowTraits.emplace_back(pumex::WindowTraits{ 0, 1110, 100, 512, 384, pumex::WindowTraits::WINDOW, "Object culling on GPU 3" });
    }
    else if (renderVRwindows)
    {
      windowTraits.emplace_back(pumex::WindowTraits{ 0, 0, 0, 100, 100, pumex::WindowTraits::HALFSCREEN_LEFT, "Object culling on GPU L" });
      windowTraits.emplace_back(pumex::WindowTraits{ 0, 100, 0, 100, 100, pumex::WindowTraits::HALFSCREEN_RIGHT, "Object culling on GPU R" });
    }
    else
    {
      windowTraits.emplace_back(pumex::WindowTraits{ 0, 100, 100, 640, 480, useFullScreen ? pumex::WindowTraits::FULLSCREEN : pumex::WindowTraits::WINDOW, "Object culling on GPU" });
    }

    std::vector<float> queuePriorities;
    queuePriorities.resize(windowTraits.size(), 0.75f);
    std::vector<pumex::QueueTraits> requestQueues = { { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, queuePriorities } };
    std::vector<const char*> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device = viewer->addDevice(0, requestQueues, requestDeviceExtensions);
    CHECK_LOG_THROW(!device->isValid(), "Cannot create logical device with requested parameters" );

    std::vector<std::shared_ptr<pumex::Window>> windows;
    for (const auto& t : windowTraits)
      windows.push_back(pumex::Window::createWindow(t));

    std::vector<pumex::FrameBufferImageDefinition> frameBufferDefinitions =
    {
      { pumex::FrameBufferImageDefinition::SwapChain, VK_FORMAT_B8G8R8A8_UNORM,    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,         VK_IMAGE_ASPECT_COLOR_BIT,                               VK_SAMPLE_COUNT_1_BIT },
      { pumex::FrameBufferImageDefinition::Depth,     VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, VK_SAMPLE_COUNT_1_BIT }
    };

    // allocate 16 MB for frame buffers ( actually only depth buffer will be allocated )
    std::shared_ptr<pumex::DeviceMemoryAllocator> frameBufferAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 16 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    std::shared_ptr<pumex::FrameBufferImages> frameBufferImages = std::make_shared<pumex::FrameBufferImages>(frameBufferDefinitions, frameBufferAllocator);

    std::vector<pumex::AttachmentDefinition> renderPassAttachments =
    {
      { 0, VK_FORMAT_B8G8R8A8_UNORM,    VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0 },
      { 1, VK_FORMAT_D24_UNORM_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED,       0 }
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

    pumex::SurfaceTraits surfaceTraits{ 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, VK_PRESENT_MODE_MAILBOX_KHR, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    surfaceTraits.definePresentationQueue(pumex::QueueTraits{ VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0,{ 0.75f } });
    surfaceTraits.setDefaultRenderPass(renderPass);
    surfaceTraits.setFrameBufferImages(frameBufferImages);

    std::shared_ptr<GpuCullApplicationData> applicationData = std::make_shared<GpuCullApplicationData>(viewer);
    applicationData->defaultRenderPass = renderPass;
    applicationData->setup(showStaticRendering,showDynamicRendering, staticAreaSize, dynamicAreaSize, lodModifier, densityModifier, triangleModifier);
    if (render3windows)
    {
      applicationData->setSlaveViewMatrix(0, glm::rotate(glm::mat4(), glm::radians(-75.16f), glm::vec3(0.0f, 1.0f, 0.0f)));
      applicationData->setSlaveViewMatrix(1, glm::mat4());
      applicationData->setSlaveViewMatrix(2, glm::rotate(glm::mat4(), glm::radians(75.16f), glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    else if (renderVRwindows)
    {
      applicationData->setSlaveViewMatrix(0, glm::translate(glm::mat4(), glm::vec3(0.03f, 0.0f, 0.0f)));
      applicationData->setSlaveViewMatrix(1, glm::translate(glm::mat4(), glm::vec3(-0.03f, 0.0f, 0.0f)));
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
      applicationData->update(pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()), pumex::inSeconds(viewer->getUpdateDuration()));
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
      auto staticBeginTime = applicationData->now();
      applicationData->fillFPS();
      if (applicationData->_showStaticRendering)
        applicationData->prepareStaticBuffersForRendering();
      auto dynamicBeginTime = applicationData->setTime(2010, staticBeginTime);
      if (applicationData->_showDynamicRendering)
        applicationData->prepareDynamicBuffersForRendering();
      applicationData->setTime(2011, dynamicBeginTime);

    });
    std::vector<tbb::flow::continue_node< tbb::flow::continue_msg >> startSurfaceFrame, drawSurfaceFrame, endSurfaceFrame;
    for (auto& surf : surfaces)
    {

      startSurfaceFrame.emplace_back(viewer->renderGraph, [=](tbb::flow::continue_msg)
      {
        auto t = applicationData->now();
        applicationData->prepareCameraForRendering(surf);
        surf->beginFrame();
        applicationData->setTime(2020 + surf->getID(), t);
      });
      drawSurfaceFrame.emplace_back(viewer->renderGraph, [=](tbb::flow::continue_msg)
      {
        auto t = applicationData->now();
        applicationData->draw(surf);
        applicationData->setTime(2030 + surf->getID(), t);
      });
      endSurfaceFrame.emplace_back(viewer->renderGraph, [=](tbb::flow::continue_msg)
      {
        auto t = applicationData->now();
        surf->endFrame();
        applicationData->setTime(2040 + surf->getID(), t);
      });
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
