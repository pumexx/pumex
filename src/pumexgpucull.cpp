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

const uint32_t MAIN_RENDER_MASK = 1;

const uint32_t STATIC_GROUND_TYPE_ID   = 1;
const uint32_t STATIC_CONIFER_TREE_ID  = 2;
const uint32_t STATIC_DECIDOUS_TREE_ID = 3;
const uint32_t STATIC_SIMPLE_HOUSE_ID  = 4;

const uint32_t DYNAMIC_BLIMP_ID = 1;
const uint32_t DYNAMIC_CAR_ID = 2;
const uint32_t DYNAMIC_AIRPLANE_ID = 3;

// struct storing the whole information required by CPU and GPU to render a single static object ( trees and buildings )
struct StaticInstanceData
{
  StaticInstanceData(const glm::mat4& p = glm::mat4(), uint32_t i = 0, uint32_t t = 0, uint32_t m = 0, float b=1.0f, float wa=0.0f, float wf=1.0f, float wo=0.0f)
    : position{ p }, id{ i, t, m, 0 }, params{b, wa, wf, wo}
  {
  }
  glm::vec3 getPosition() const
  {
    glm::vec4 pos4 = position * glm::vec4(0.0, 0.0, 0.0, 1.0);
    pos4 /= pos4.w;
    return glm::vec3(pos4.x, pos4.y, pos4.z);
  }
  glm::uvec4 id;     // id, typeID, materialVariant, 0
  glm::vec4  params; // brightness, wavingAmplitude, wavingFrequency, wavingOffset
  glm::mat4  position;
};

const uint32_t MAX_BONES = 9;

// struct storing information about dynamic object used during update phase
struct DynamicObjectData
{
  pumex::Kinematic kinematic;
  uint32_t         id;
  uint32_t         typeID;
  uint32_t         materialVariant;
  float            time2NextTurn;
  float            brightness;
};

// struct storing the whole information required by GPU to render a single dynamic object
struct DynamicInstanceData
{
  DynamicInstanceData(const glm::mat4& p = glm::mat4(), uint32_t i = 0, uint32_t t = 0, uint32_t m = 0, float b=1.0f)
    : position{ p }, id{ i, t, m, 0  }, params{ b, 0.0f, 0.0f, 0.0f }
  {
  }
  glm::uvec4 id;     // id, typeId, materialVariant, 0
  glm::vec4  params; // brightness, 0, 0, 0
  glm::mat4  position;
  glm::mat4  bones[MAX_BONES];
};

class XXX
{
public:
  virtual DynamicInstanceData update(const DynamicObjectData& objectData, float deltaTime, float renderTime) = 0;
};

class BlimpXXX : public XXX
{
public:
  BlimpXXX(const std::vector<glm::mat4>& bonesReset, size_t blimpPropL, size_t blimpPropR)
    : _bonesReset{ bonesReset }, _blimpPropL{ blimpPropL }, _blimpPropR{ blimpPropR }
  {
  }
  DynamicInstanceData update(const DynamicObjectData& objectData, float deltaTime, float renderTime) override
  {
    DynamicInstanceData diData(pumex::extrapolate(objectData.kinematic, deltaTime), objectData.id, objectData.typeID, objectData.materialVariant, objectData.brightness);
    diData.bones[_blimpPropL] = _bonesReset[_blimpPropL] * glm::rotate(glm::mat4(), fmodf(glm::two_pi<float>() *  0.5f * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
    diData.bones[_blimpPropR] = _bonesReset[_blimpPropR] * glm::rotate(glm::mat4(), fmodf(glm::two_pi<float>() * -0.5f * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
    return diData;
  }
protected:
  std::vector<glm::mat4> _bonesReset;
  size_t                 _blimpPropL;
  size_t                 _blimpPropR;
};

class CarXXX : public XXX
{
public:
  CarXXX(const std::vector<glm::mat4>& bonesReset, size_t carWheel0, size_t carWheel1, size_t carWheel2, size_t carWheel3)
    : _bonesReset{ bonesReset }, _carWheel0{ carWheel0 }, _carWheel1{ carWheel1 }, _carWheel2{ carWheel2 }, _carWheel3{ carWheel3 }
  {
  }
  DynamicInstanceData update(const DynamicObjectData& objectData, float deltaTime, float renderTime) override
  {
    DynamicInstanceData diData(pumex::extrapolate(objectData.kinematic, deltaTime), objectData.id, objectData.typeID, objectData.materialVariant, objectData.brightness);
    float speed = glm::length(objectData.kinematic.velocity);
    diData.bones[_carWheel0] = _bonesReset[_carWheel0] * glm::rotate(glm::mat4(), fmodf((speed / 0.5f) * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
    diData.bones[_carWheel1] = _bonesReset[_carWheel1] * glm::rotate(glm::mat4(), fmodf((speed / 0.5f) * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
    diData.bones[_carWheel2] = _bonesReset[_carWheel2] * glm::rotate(glm::mat4(), fmodf((-speed / 0.5f) * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
    diData.bones[_carWheel3] = _bonesReset[_carWheel3] * glm::rotate(glm::mat4(), fmodf((-speed / 0.5f) * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
    return diData;
  }
protected:
  std::vector<glm::mat4> _bonesReset;
  size_t                 _carWheel0;
  size_t                 _carWheel1;
  size_t                 _carWheel2;
  size_t                 _carWheel3;
};

class AirplaneXXX : public XXX
{
public:
  AirplaneXXX(const std::vector<glm::mat4>& bonesReset, size_t airplaneProp)
    : _bonesReset{ bonesReset }, _airplaneProp{ airplaneProp }
  {
  }
  DynamicInstanceData update(const DynamicObjectData& objectData, float deltaTime, float renderTime) override
  {
    DynamicInstanceData diData(pumex::extrapolate(objectData.kinematic, deltaTime), objectData.id, objectData.typeID, objectData.materialVariant, objectData.brightness);
    diData.bones[_airplaneProp] = _bonesReset[_airplaneProp] * glm::rotate(glm::mat4(), fmodf(glm::two_pi<float>() *  -1.5f * renderTime, glm::two_pi<float>()), glm::vec3(0.0, 0.0, 1.0));
    return diData;
  }
protected:
  std::vector<glm::mat4> _bonesReset;
  size_t                 _airplaneProp;
};

// The purpose of InstanceCell class is to store instances and to divide it into a tree.
template<typename T>
class InstanceCell
{
public:
  typedef std::vector< std::shared_ptr<InstanceCell<T> > > InstanceCellList;

  InstanceCell()
    : parent(nullptr) 
  {
  }
    
  InstanceCell(const pumex::BoundingBox& bb)
    :parent(nullptr), bb(bb) 
  {
  }
    
  void addCell(std::shared_ptr<InstanceCell> cell) 
  { 
    cell->parent=this; 
    cells.push_back(cell); 
  }

  void computeBound()
  {
    bb = pumex::BoundingBox();
    for (auto citr = begin(cells); citr != end(cells);  ++citr)
    {
      (*citr)->computeBound();
      bb += (*citr)->bb;
    }

    for (auto titr = begin(instances); titr != end(instances); ++titr)
      bb += titr->getPosition();
  }

  bool contains(const glm::vec3& position) const 
  { 
    return bb.contains(position); 
  }

  bool divide(unsigned int maxNumInstancesPerCell=1000)
  {
    if (instances.size() <= maxNumInstancesPerCell) return false;

    computeBound();

    float radius = bb.radius();
    float divide_distance = radius * 0.7f;
    if (divide((bb.bbMax.x - bb.bbMin.x) > divide_distance, (bb.bbMax.y - bb.bbMin.y)>divide_distance, (bb.bbMax.z - bb.bbMin.z)>divide_distance))
    {
      // recusively divide the new cells till maxNumInstancesPerCell is met.
      for (auto citr = begin(cells); citr != end(cells); ++citr)
        (*citr)->divide(maxNumInstancesPerCell);
      return true;
    }
    else
      return false;
  }

  bool divide(bool xAxis, bool yAxis, bool zAxis)
  {
    if (!(xAxis || yAxis || zAxis)) return false;

    if (cells.empty())
      cells.push_back(std::make_shared<InstanceCell<T>>(bb));

    if (xAxis)
    {
      std::vector<std::shared_ptr<InstanceCell<T>>> newCells;
      for (auto& orig_cell : cells)
      {
        auto new_cell = std::make_shared<InstanceCell<T>>(orig_cell->bb);

        float xCenter = (orig_cell->bb.bbMin.x + orig_cell->bb.bbMax.x)*0.5f;
        orig_cell->bb.bbMax.x = xCenter;
        new_cell->bb.bbMin.x = xCenter;

        newCells.push_back(new_cell);
      }
      std::copy(begin(newCells), end(newCells), std::back_inserter(cells));
    }

    if (yAxis)
    {
      std::vector<std::shared_ptr<InstanceCell<T>>> newCells;
      for (auto& orig_cell : cells)
      {
        auto new_cell = std::make_shared<InstanceCell<T>>(orig_cell->bb);

        float yCenter = (orig_cell->bb.bbMin.y + orig_cell->bb.bbMax.y)*0.5f;
        orig_cell->bb.bbMax.y = yCenter;
        new_cell->bb.bbMin.y = yCenter;

        newCells.push_back(new_cell);
      }
      std::copy(begin(newCells), end(newCells), std::back_inserter(cells));
    }

    if (zAxis)
    {
      std::vector<std::shared_ptr<InstanceCell<T>>> newCells;
      for (auto& orig_cell : cells)
      {
        auto new_cell = std::make_shared<InstanceCell<T>>(orig_cell->bb);

        float zCenter = (orig_cell->bb.bbMin.z + orig_cell->bb.bbMax.z)*0.5f;
        orig_cell->bb.bbMax.z = zCenter;
        new_cell->bb.bbMin.z = zCenter;

        newCells.push_back(new_cell);
      }
      std::copy(begin(newCells), end(newCells), std::back_inserter(cells));
    }
    bin();
    return true;
  }

  void bin()
  {
    // put treeste cells.
    std::vector<T> instancesNotAssigned;
    for (auto titr = begin(instances); titr != end(instances); ++titr)
    {
      glm::vec3 iPosition = titr->getPosition();
      bool assigned = false;
      for (auto citr = begin(cells); citr != end(cells) && !assigned; ++citr)
      {
        if ((*citr)->contains(iPosition))
        {
          (*citr)->instances.push_back(*titr);
          assigned = true;
        }
      }
      if (!assigned) instancesNotAssigned.push_back(*titr);
    }

    // put the unassigned trees back into the original local tree list.
    instances.swap(instancesNotAssigned);

    // remove empty cells
    cells.erase(std::remove_if(begin(cells), end(cells), [](std::shared_ptr<InstanceCell<T>> cell) { return cell->instances.empty(); }), end(cells));
  }

  InstanceCell*       parent;
  pumex::BoundingBox  bb;
  InstanceCellList    cells;
  std::vector<T>      instances;
};

template<typename T>
std::shared_ptr<pumex::Node> createInstanceGraph( std::shared_ptr<InstanceCell<T>> cell, const pumex::BoundingBox& objectsBBox, std::shared_ptr<pumex::DeviceMemoryAllocator> bufferAllocator, std::shared_ptr<pumex::DescriptorSetLayout> filterDescriptorSetLayout)
{
  bool needGroup = !(cell->cells.empty());
  bool needInstances = !(cell->instances.empty());

  std::shared_ptr<pumex::Group>        group;
  std::shared_ptr<pumex::DispatchNode> dNode;

  if (needInstances)
  {
    uint32_t instanceCount = cell->instances.size();
    dNode = std::make_shared<pumex::DispatchNode>(instanceCount / 16 + ((instanceCount % 16 > 0) ? 1 : 0), 1, 1);

    auto xin = std::make_shared<std::vector<T>>();
    *xin = cell->instances;
    auto storageBuffer = std::make_shared<pumex::Buffer<std::vector<T>>>(xin, bufferAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pumex::pbPerDevice, pumex::swOnce);

    auto staticFilterDescriptorSet = std::make_shared<pumex::DescriptorSet>(filterDescriptorSetLayout);
    staticFilterDescriptorSet->setDescriptor(0, std::make_shared<pumex::StorageBuffer>(storageBuffer));
    dNode->setDescriptorSet(1, staticFilterDescriptorSet);
  }

  if (needGroup)
  {
    group = std::make_shared<pumex::Group>();
    for (auto itr = begin(cell->cells); itr != end(cell->cells); ++itr)
      group->addChild(createInstanceGraph(*itr, objectsBBox, bufferAllocator, filterDescriptorSetLayout));
    if (dNode != nullptr) 
      group->addChild(dNode);
  }
  if (group != nullptr) return group;
  else return dNode;
}

template <typename T>
std::shared_ptr<pumex::Node> createInstanceTree(const std::vector<T>& instances, const pumex::BoundingBox& objectsBBox, unsigned int maxNumInstancesPerCell, std::shared_ptr<pumex::DeviceMemoryAllocator> bufferAllocator, std::shared_ptr<pumex::DescriptorSetLayout> filterDescriptorSetLayout )
{
  auto rootCell = std::make_shared<InstanceCell<T>>();
  rootCell->instances = instances;
  rootCell->divide( maxNumInstancesPerCell );
    
  return createInstanceGraph( rootCell, objectsBBox, bufferAllocator, filterDescriptorSetLayout);
}

// we are trying to count how many objects of each type there is in an instance tree
class TypeCountVisitor : public pumex::NodeVisitor
{
public:
  TypeCountVisitor(uint32_t numTypes, uint32_t dsIndex, uint32_t dIndex)
    : pumex::NodeVisitor{pumex::NodeVisitor::AllChildren}, descriptorSetIndex{ dsIndex }, descriptorIndex{ dIndex }
  {
    typeCount.resize(numTypes, 0);
  }

  void apply(pumex::Node& node) override
  {
    auto descriptorSet = node.getDescriptorSet(descriptorSetIndex);
    if (descriptorSet != nullptr)
    {
      auto descriptor = descriptorSet->getDescriptor(descriptorIndex);
      if (descriptor != nullptr)
      {
        for (auto& res : descriptor->resources)
        {
          auto sb = std::dynamic_pointer_cast<pumex::StorageBuffer>(res);
          if (sb != nullptr)
          {
            auto buf = std::dynamic_pointer_cast<pumex::Buffer<std::vector<StaticInstanceData>>>(sb->memoryBuffer);
            if (buf != nullptr)
            {
              auto data = buf->getData();
              for (auto& instance : *data)
              {
                typeCount[instance.id[1]]++;
              }
            }
          }
        }
      }
    }
    traverse(node);
  }

  std::vector<size_t> typeCount;
  uint32_t            descriptorSetIndex;
  uint32_t            descriptorIndex;
};

void resizeStaticOutputBuffers(std::shared_ptr<pumex::Buffer<std::vector<StaticInstanceData>>> buffer, std::shared_ptr<pumex::Buffer<std::vector<uint32_t>>> indexBuffer, uint32_t mask, size_t instanceCount)
{
  switch (mask)
  {
  case MAIN_RENDER_MASK:
    buffer->setData(std::vector<StaticInstanceData>(instanceCount));
    indexBuffer->setData(std::vector<uint32_t>(3*instanceCount));
    break;
  }
}

void resizeDynamicOutputBuffers(std::shared_ptr<pumex::Buffer<std::vector<uint32_t>>> buffer, std::shared_ptr<pumex::DispatchNode> dispatchNode, uint32_t mask, size_t instanceCount)
{
  switch (mask)
  {
  case MAIN_RENDER_MASK:
    buffer->setData(std::vector<uint32_t>(instanceCount));
    dispatchNode->setDispatch(instanceCount / 16 + ((instanceCount % 16 > 0) ? 1 : 0), 1, 1);
    break;
  }
}

struct UpdateData
{
  UpdateData()
  {
  }
  glm::vec3                      cameraPosition;
  glm::vec2                      cameraGeographicCoordinates;
  float                          cameraDistance;

  std::vector<DynamicObjectData> dynamicObjectData;

  glm::vec2                      lastMousePos;
  bool                           leftMouseKeyPressed;
  bool                           rightMouseKeyPressed;

  bool                           moveForward;
  bool                           moveBackward;
  bool                           moveLeft;
  bool                           moveRight;
  bool                           moveUp;
  bool                           moveDown;
  bool                           moveFast;
  bool                           measureTime;
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

  std::vector<DynamicObjectData> dynamicObjectData;
};

// struct that stores information about material used by specific object type. This example does not use textures ( in contrast to crowd example )
struct MaterialGpuCull
{
  glm::vec4 ambient;
  glm::vec4 diffuse;
  glm::vec4 specular;
  glm::vec4 shininess;

  // two functions that define material parameters according to data from an asset's material 
  void registerProperties(const pumex::Material& material)
  {
    ambient   = material.getProperty("$clr.ambient", glm::vec4(0, 0, 0, 0));
    diffuse   = material.getProperty("$clr.diffuse", glm::vec4(1, 1, 1, 1));
    specular  = material.getProperty("$clr.specular", glm::vec4(0, 0, 0, 0));
    shininess = material.getProperty("$mat.shininess", glm::vec4(0, 0, 0, 0));
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
  pumex::addBox(building, glm::vec3(-7.5f, -4.5f, 0.0f), glm::vec3(7.5f, 4.5f, 16.0f), true);
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
    std::transform(begin(oneProp.indices), end(oneProp.indices), std::back_inserter(propeller.indices), [=](uint32_t x){ return verticesSoFar + x; });
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
  pumex::addBox(hull, glm::vec3(-4.0, -0.15, -12.0), glm::vec3(4.0, 0.15, -8.0), true);
  pumex::addBox(hull, glm::vec3(-0.15, -4.0, -12.0), glm::vec3(0.15, 4.0, -8.0), true);
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
  pumex::addBox(hull, glm::vec3(-2.5, -1.5, 0.4), glm::vec3(2.5, 1.5, 2.7), true);
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
    pumex::createSimpleAsset(wheel, "wheel0"),
    pumex::createSimpleAsset(wheel, "wheel1"),
    pumex::createSimpleAsset(wheel, "wheel2"),
    pumex::createSimpleAsset(wheel, "wheel3")
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
  pumex::addBox(hull, glm::vec3(0.35, -3.5, 0.5), glm::vec3(0.45, 3.5, 2.1), true);
  pumex::addBox(hull, glm::vec3(-1.45, -5.0, 0.6), glm::vec3(-1.35, 5.0, 2.4), true);
  // add rudders
  pumex::addBox(hull, glm::vec3(-1.55, -0.025, -4.4), glm::vec3(-0.05, 0.025, -3.4), true);
  pumex::addBox(hull, glm::vec3(-0.225, -2.0, -4.4), glm::vec3(-0.175, 2.0, -3.4), true);
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
  bool      _showStaticRendering  = false;
  bool      _showDynamicRendering = false;
  uint32_t  _instancesPerCell     = 4096;
  float     _dynamicAreaSize;
  glm::vec2 _minArea;
  glm::vec2 _maxArea;

  std::shared_ptr<pumex::Buffer<std::vector<pumex::DrawIndexedIndirectCommand>>> _staticDrawCommands;
  std::shared_ptr<pumex::Buffer<uint32_t>>                            _staticCounterBuffer;
  std::exponential_distribution<float>                                _randomTime2NextTurn;
  std::uniform_real_distribution<float>                               _randomRotation;
  std::unordered_map<uint32_t, std::uniform_real_distribution<float>> _randomObjectSpeed;
  std::default_random_engine                                          _randomEngine;

  UpdateData                                                          updateData;
  std::array<RenderData, 3>                                           renderData;

  std::shared_ptr<pumex::AssetBufferFilterNode>                       _dynamicFilterNode;

  std::shared_ptr<pumex::Buffer<pumex::Camera>>                       cameraBuffer;
  std::shared_ptr<pumex::Buffer<pumex::Camera>>                       textCameraBuffer;
  std::shared_ptr<pumex::Buffer<std::vector<DynamicInstanceData>>>    dynamicInstanceBuffer;

  std::vector<uint32_t>                                               _staticTypeIDs;
  std::unordered_map<uint32_t, std::shared_ptr<XXX>>                  _dynamicTypeIDs;

  pumex::HPClock::time_point                           lastFrameStart;
  bool                                                 measureTime = true;
  std::mutex                                           measureMutex;
  std::unordered_map<uint32_t, double>                 times;

  std::unordered_map<uint32_t, glm::mat4>              slaveViewMatrix;

  GpuCullApplicationData(std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator)
    : _randomTime2NextTurn{ 0.1f }, _randomRotation(-glm::pi<float>(), glm::pi<float>())
  {
    cameraBuffer          = std::make_shared<pumex::Buffer<pumex::Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce, true);
    textCameraBuffer      = std::make_shared<pumex::Buffer<pumex::Camera>>(buffersAllocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce, true);
    dynamicInstanceBuffer = std::make_shared<pumex::Buffer<std::vector<DynamicInstanceData>>>(std::make_shared<std::vector<DynamicInstanceData>>(), buffersAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pumex::pbPerDevice, pumex::swForEachImage);

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

  void setupStaticModels(float staticAreaSize, float lodModifier, float triangleModifier, std::shared_ptr<pumex::AssetBuffer> staticAssetBuffer, std::shared_ptr<pumex::MaterialSet> staticMaterialSet)
  {
    _showStaticRendering = true;

    std::shared_ptr<pumex::Asset> groundAsset(createGround(staticAreaSize, glm::vec4(0.0f, 0.7f, 0.0f, 1.0f)));
    pumex::BoundingBox groundBbox = pumex::calculateBoundingBox(*groundAsset, MAIN_RENDER_MASK);
    staticAssetBuffer->registerType(STATIC_GROUND_TYPE_ID, pumex::AssetTypeDefinition(groundBbox));
    staticMaterialSet->registerMaterials(STATIC_GROUND_TYPE_ID, groundAsset);
    staticAssetBuffer->registerObjectLOD(STATIC_GROUND_TYPE_ID, pumex::AssetLodDefinition(0.0f, 5.0f * staticAreaSize), groundAsset );

    std::shared_ptr<pumex::Asset> coniferTree0 ( createConiferTree( 0.75f * triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> coniferTree1 ( createConiferTree(0.45f * triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> coniferTree2 ( createConiferTree(0.15f * triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox coniferTreeBbox = pumex::calculateBoundingBox(*coniferTree0, MAIN_RENDER_MASK);
    staticAssetBuffer->registerType(STATIC_CONIFER_TREE_ID, pumex::AssetTypeDefinition(coniferTreeBbox));
    staticMaterialSet->registerMaterials(STATIC_CONIFER_TREE_ID, coniferTree0);
    staticMaterialSet->registerMaterials(STATIC_CONIFER_TREE_ID, coniferTree1);
    staticMaterialSet->registerMaterials(STATIC_CONIFER_TREE_ID, coniferTree2);
    staticAssetBuffer->registerObjectLOD(STATIC_CONIFER_TREE_ID, pumex::AssetLodDefinition(0.0f * lodModifier, 100.0f * lodModifier), coniferTree0 );
    staticAssetBuffer->registerObjectLOD(STATIC_CONIFER_TREE_ID, pumex::AssetLodDefinition(100.0f * lodModifier, 500.0f * lodModifier), coniferTree1 );
    staticAssetBuffer->registerObjectLOD(STATIC_CONIFER_TREE_ID, pumex::AssetLodDefinition(500.0f * lodModifier, 1200.0f * lodModifier), coniferTree2 );
    _staticTypeIDs.push_back(STATIC_CONIFER_TREE_ID);

    std::shared_ptr<pumex::Asset> decidousTree0 ( createDecidousTree(0.75f * triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> decidousTree1 ( createDecidousTree(0.45f * triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> decidousTree2 ( createDecidousTree(0.15f * triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox decidousTreeBbox = pumex::calculateBoundingBox(*decidousTree0, MAIN_RENDER_MASK);
    staticAssetBuffer->registerType(STATIC_DECIDOUS_TREE_ID, pumex::AssetTypeDefinition(decidousTreeBbox));
    staticMaterialSet->registerMaterials(STATIC_DECIDOUS_TREE_ID, decidousTree0);
    staticMaterialSet->registerMaterials(STATIC_DECIDOUS_TREE_ID, decidousTree1);
    staticMaterialSet->registerMaterials(STATIC_DECIDOUS_TREE_ID, decidousTree2);
    staticAssetBuffer->registerObjectLOD(STATIC_DECIDOUS_TREE_ID, pumex::AssetLodDefinition(   0.0f * lodModifier,  120.0f * lodModifier ), decidousTree0);
    staticAssetBuffer->registerObjectLOD(STATIC_DECIDOUS_TREE_ID, pumex::AssetLodDefinition( 120.0f * lodModifier,  600.0f * lodModifier ), decidousTree1);
    staticAssetBuffer->registerObjectLOD(STATIC_DECIDOUS_TREE_ID, pumex::AssetLodDefinition( 600.0f * lodModifier, 1400.0f * lodModifier ), decidousTree2);
    _staticTypeIDs.push_back(STATIC_DECIDOUS_TREE_ID);

    std::shared_ptr<pumex::Asset> simpleHouse0 ( createSimpleHouse(0.75f * triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> simpleHouse1 ( createSimpleHouse(0.45f * triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> simpleHouse2 ( createSimpleHouse(0.15f * triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox simpleHouseBbox = pumex::calculateBoundingBox(*simpleHouse0, MAIN_RENDER_MASK);
    staticAssetBuffer->registerType(STATIC_SIMPLE_HOUSE_ID, pumex::AssetTypeDefinition(simpleHouseBbox));
    staticMaterialSet->registerMaterials(STATIC_SIMPLE_HOUSE_ID, simpleHouse0);
    staticMaterialSet->registerMaterials(STATIC_SIMPLE_HOUSE_ID, simpleHouse1);
    staticMaterialSet->registerMaterials(STATIC_SIMPLE_HOUSE_ID, simpleHouse2);
    staticAssetBuffer->registerObjectLOD(STATIC_SIMPLE_HOUSE_ID, pumex::AssetLodDefinition(  0.0f * lodModifier,  120.0f * lodModifier), simpleHouse0);
    staticAssetBuffer->registerObjectLOD(STATIC_SIMPLE_HOUSE_ID, pumex::AssetLodDefinition(120.0f * lodModifier,  600.0f * lodModifier), simpleHouse1);
    staticAssetBuffer->registerObjectLOD(STATIC_SIMPLE_HOUSE_ID, pumex::AssetLodDefinition(600.0f * lodModifier, 1400.0f * lodModifier), simpleHouse2);
    _staticTypeIDs.push_back(STATIC_SIMPLE_HOUSE_ID);

    staticMaterialSet->endRegisterMaterials();
  }

  std::shared_ptr<pumex::Node> setupStaticInstances(float staticAreaSize, float densityModifier, uint32_t instancesPerCell, std::shared_ptr<pumex::AssetBufferFilterNode> staticAssetBufferFilterNode, std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator, std::shared_ptr<pumex::DescriptorSetLayout> staticFilterDescriptorSetLayout1)
  {
    std::map<uint32_t,float> objectDensity =
    { 
      { STATIC_CONIFER_TREE_ID, 10000.0f * densityModifier}, 
      { STATIC_DECIDOUS_TREE_ID, 1000.0f * densityModifier}, 
      { STATIC_SIMPLE_HOUSE_ID, 100.0f * densityModifier }
    };
    std::map<uint32_t, float> amplitudeModifier =
    { 
      { STATIC_CONIFER_TREE_ID,  1.0f },
      { STATIC_DECIDOUS_TREE_ID, 1.0f },
      { STATIC_SIMPLE_HOUSE_ID,  0.0f }  // we don't want the house to wave in the wind
    }; 

    float fullArea = staticAreaSize * staticAreaSize;
    std::uniform_real_distribution<float> randomX(-0.5f*staticAreaSize, 0.5f * staticAreaSize);
    std::uniform_real_distribution<float> randomY(-0.5f*staticAreaSize, 0.5f * staticAreaSize);
    std::uniform_real_distribution<float> randomRotation(-glm::pi<float>(), glm::pi<float>());
    std::uniform_real_distribution<float> randomScale(0.8f, 1.2f);
    std::uniform_real_distribution<float> randomBrightness(0.5f, 1.0f);
    std::uniform_real_distribution<float> randomAmplitude(0.01f, 0.05f);
    std::uniform_real_distribution<float> randomFrequency(0.1f * glm::two_pi<float>(), 0.5f * glm::two_pi<float>());
    std::uniform_real_distribution<float> randomOffset(0.0f * glm::two_pi<float>(), 1.0f * glm::two_pi<float>());
    uint32_t id = 1;

    std::vector<StaticInstanceData> staticInstanceData;
    pumex::BoundingBox allObjectsBBox;

    staticInstanceData.emplace_back(StaticInstanceData(glm::mat4(), id++, STATIC_GROUND_TYPE_ID, 0, 1.0f, 0.0f, 1.0f, 0.0f));
    for (auto it = begin(_staticTypeIDs); it != end(_staticTypeIDs); ++it)
    {
      int objectQuantity = (int)floor(objectDensity[*it] * fullArea / 1000000.0f);

      for (int j = 0; j<objectQuantity; ++j)
      {
        glm::vec3 pos( randomX(_randomEngine), randomY(_randomEngine), 0.0f );
        float rot             = randomRotation(_randomEngine);
        float scale           = randomScale(_randomEngine);
        float brightness      = randomBrightness(_randomEngine);
        float wavingAmplitude = randomAmplitude(_randomEngine) * amplitudeModifier[*it];
        float wavingFrequency = randomFrequency(_randomEngine);
        float wavingOffset    = randomOffset(_randomEngine);
        glm::mat4 position(glm::translate(glm::mat4(), glm::vec3(pos.x, pos.y, pos.z)) * glm::rotate(glm::mat4(), rot, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::scale(glm::mat4(), glm::vec3(scale, scale, scale)));
        staticInstanceData.emplace_back(StaticInstanceData(position, id++, *it, 0, brightness, wavingAmplitude, wavingFrequency, wavingOffset));
        allObjectsBBox        += pos;
      }
    }
    std::shared_ptr<pumex::Node> instanceTree = createInstanceTree(staticInstanceData, allObjectsBBox, instancesPerCell, buffersAllocator, staticFilterDescriptorSetLayout1);

    uint32_t maxType = *std::max_element(begin(_staticTypeIDs), end(_staticTypeIDs));
    TypeCountVisitor tcv(maxType + 1, 1, 0);
    instanceTree->accept(tcv);
    staticAssetBufferFilterNode->setTypeCount(tcv.typeCount);

    return instanceTree;
  }

  void setupStaticBuffers(std::shared_ptr<pumex::Buffer<uint32_t>> staticCounterBuffer, std::shared_ptr<pumex::Buffer<std::vector<pumex::DrawIndexedIndirectCommand>>> staticDrawCommands)
  {
    _staticCounterBuffer = staticCounterBuffer;
    _staticDrawCommands  = staticDrawCommands;
  }

  void setupDynamicModels(float lodModifier, float triangleModifier, std::shared_ptr<pumex::AssetBuffer> dynamicAssetBuffer, std::shared_ptr<pumex::MaterialSet> dynamicMaterialSet)
  {
    _showDynamicRendering = true;

    std::shared_ptr<pumex::Asset> blimpLod0 ( createBlimp(0.75f * triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)) );
    std::shared_ptr<pumex::Asset> blimpLod1 ( createBlimp(0.45f * triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)) );
    std::shared_ptr<pumex::Asset> blimpLod2 ( createBlimp(0.20f * triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)) );
    pumex::BoundingBox blimpBbox = pumex::calculateBoundingBox(*blimpLod0, MAIN_RENDER_MASK);
    dynamicAssetBuffer->registerType(DYNAMIC_BLIMP_ID, pumex::AssetTypeDefinition(blimpBbox));
    dynamicMaterialSet->registerMaterials(DYNAMIC_BLIMP_ID, blimpLod0);
    dynamicMaterialSet->registerMaterials(DYNAMIC_BLIMP_ID, blimpLod1);
    dynamicMaterialSet->registerMaterials(DYNAMIC_BLIMP_ID, blimpLod2);
    dynamicAssetBuffer->registerObjectLOD(DYNAMIC_BLIMP_ID, pumex::AssetLodDefinition(  0.0f * lodModifier,  150.0f * lodModifier), blimpLod0);
    dynamicAssetBuffer->registerObjectLOD(DYNAMIC_BLIMP_ID, pumex::AssetLodDefinition(150.0f * lodModifier,  800.0f * lodModifier), blimpLod1);
    dynamicAssetBuffer->registerObjectLOD(DYNAMIC_BLIMP_ID, pumex::AssetLodDefinition(800.0f * lodModifier, 6500.0f * lodModifier), blimpLod2);
    _dynamicTypeIDs.insert({ DYNAMIC_BLIMP_ID, std::make_shared<BlimpXXX>(pumex::calculateResetPosition(*blimpLod0), blimpLod0->skeleton.invBoneNames["propL"],blimpLod0->skeleton.invBoneNames["propR"]) });

    std::shared_ptr<pumex::Asset> carLod0(createCar(0.75f * triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.3, 0.3, 0.3, 1.0)));
    std::shared_ptr<pumex::Asset> carLod1(createCar(0.45f * triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> carLod2(createCar(0.15f * triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox carBbox = pumex::calculateBoundingBox(*carLod0, MAIN_RENDER_MASK);
    dynamicAssetBuffer->registerType(DYNAMIC_CAR_ID, pumex::AssetTypeDefinition(carBbox));
    dynamicMaterialSet->registerMaterials(DYNAMIC_CAR_ID, carLod0);
    dynamicMaterialSet->registerMaterials(DYNAMIC_CAR_ID, carLod1);
    dynamicMaterialSet->registerMaterials(DYNAMIC_CAR_ID, carLod2);
    dynamicAssetBuffer->registerObjectLOD(DYNAMIC_CAR_ID, pumex::AssetLodDefinition(  0.0f * lodModifier,   50.0f * lodModifier), carLod0);
    dynamicAssetBuffer->registerObjectLOD(DYNAMIC_CAR_ID, pumex::AssetLodDefinition( 50.0f * lodModifier,  300.0f * lodModifier), carLod1);
    dynamicAssetBuffer->registerObjectLOD(DYNAMIC_CAR_ID, pumex::AssetLodDefinition(300.0f * lodModifier, 1000.0f * lodModifier), carLod2);
    _dynamicTypeIDs.insert({ DYNAMIC_CAR_ID, std::make_shared<CarXXX>(pumex::calculateResetPosition(*carLod0), carLod0->skeleton.invBoneNames["wheel0"], carLod0->skeleton.invBoneNames["wheel1"], carLod0->skeleton.invBoneNames["wheel2"], carLod0->skeleton.invBoneNames["wheel3"]) });

    std::shared_ptr<pumex::Asset> airplaneLod0(createAirplane(0.75f * triangleModifier, glm::vec4(1.0, 1.0, 1.0, 1.0), glm::vec4(0.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> airplaneLod1(createAirplane(0.45f * triangleModifier, glm::vec4(0.0, 0.0, 1.0, 1.0), glm::vec4(1.0, 1.0, 0.0, 1.0)));
    std::shared_ptr<pumex::Asset> airplaneLod2(createAirplane(0.15f * triangleModifier, glm::vec4(1.0, 0.0, 0.0, 1.0), glm::vec4(0.0, 0.0, 1.0, 1.0)));
    pumex::BoundingBox airplaneBbox = pumex::calculateBoundingBox(*airplaneLod0, MAIN_RENDER_MASK);
    dynamicAssetBuffer->registerType(DYNAMIC_AIRPLANE_ID, pumex::AssetTypeDefinition(airplaneBbox));
    dynamicMaterialSet->registerMaterials(DYNAMIC_AIRPLANE_ID, airplaneLod0);
    dynamicMaterialSet->registerMaterials(DYNAMIC_AIRPLANE_ID, airplaneLod1);
    dynamicMaterialSet->registerMaterials(DYNAMIC_AIRPLANE_ID, airplaneLod2);
    dynamicAssetBuffer->registerObjectLOD(DYNAMIC_AIRPLANE_ID, pumex::AssetLodDefinition(  0.0f * lodModifier,   80.0f * lodModifier), airplaneLod0);
    dynamicAssetBuffer->registerObjectLOD(DYNAMIC_AIRPLANE_ID, pumex::AssetLodDefinition( 80.0f * lodModifier,  400.0f * lodModifier), airplaneLod1);
    dynamicAssetBuffer->registerObjectLOD(DYNAMIC_AIRPLANE_ID, pumex::AssetLodDefinition(400.0f * lodModifier, 1200.0f * lodModifier), airplaneLod2);
    _dynamicTypeIDs.insert({ DYNAMIC_AIRPLANE_ID, std::make_shared<AirplaneXXX>(pumex::calculateResetPosition(*airplaneLod0), airplaneLod0->skeleton.invBoneNames["prop"]) });

    dynamicMaterialSet->endRegisterMaterials();
  }

  size_t setupDynamicInstances(float dynamicAreaSize, float densityModifier, std::shared_ptr<pumex::AssetBufferFilterNode> dynamicFilterNode)
  {
    _dynamicAreaSize   = dynamicAreaSize;
    _minArea           = glm::vec2(-0.5f*_dynamicAreaSize, -0.5f*_dynamicAreaSize);
    _maxArea           = glm::vec2(0.5f*_dynamicAreaSize, 0.5f*_dynamicAreaSize);
    _dynamicFilterNode = dynamicFilterNode;

    std::map<uint32_t, float> objectZ =
    {
      { DYNAMIC_BLIMP_ID,    50.0f },
      { DYNAMIC_CAR_ID,      0.0f },
      { DYNAMIC_AIRPLANE_ID, 25.0f }
    };
    std::map<uint32_t, float> objectDensity =
    {
      { DYNAMIC_BLIMP_ID,    100.0f * densityModifier },
      { DYNAMIC_CAR_ID,      100.0f * densityModifier },
      { DYNAMIC_AIRPLANE_ID, 100.0f * densityModifier }
    };
    std::map<uint32_t, float> minObjectSpeed =
    {
      { DYNAMIC_BLIMP_ID,    5.0f },
      { DYNAMIC_CAR_ID,      1.0f },
      { DYNAMIC_AIRPLANE_ID, 10.0f }
    };
    std::map<uint32_t, float> maxObjectSpeed =
    {
      { DYNAMIC_BLIMP_ID,    10.0f },
      { DYNAMIC_CAR_ID,      5.0f },
      { DYNAMIC_AIRPLANE_ID, 16.0f }
    };

    float fullArea = dynamicAreaSize * dynamicAreaSize;
    std::uniform_real_distribution<float> randomX(-0.5f*dynamicAreaSize, 0.5f * dynamicAreaSize);
    std::uniform_real_distribution<float> randomY(-0.5f*dynamicAreaSize, 0.5f * dynamicAreaSize);
    std::uniform_real_distribution<float> randomRotation(-glm::pi<float>(), glm::pi<float>());
    std::uniform_real_distribution<float> randomBrightness(0.5f, 1.0f);
    std::exponential_distribution<float>  randomTime2NextTurn(0.1f);

    uint32_t id    = 1;
    for(auto it = begin(_dynamicTypeIDs); it!= end(_dynamicTypeIDs); ++it)
    {
      _randomObjectSpeed.insert({ it->first,std::uniform_real_distribution<float>(minObjectSpeed[it->first], maxObjectSpeed[it->first]) });
      int objectQuantity = (int)floor(objectDensity[it->first] * fullArea / 1000000.0f);
      for (int j = 0; j<objectQuantity; ++j)
      {
        DynamicObjectData objectData;
        objectData.id                    = id++;
        objectData.typeID                = it->first;
        objectData.kinematic.position    = glm::vec3(randomX(_randomEngine), randomY(_randomEngine), objectZ[it->first]);
        objectData.kinematic.orientation = glm::angleAxis(randomRotation(_randomEngine), glm::vec3(0.0f, 0.0f, 1.0f));
        objectData.kinematic.velocity    = glm::rotate(objectData.kinematic.orientation, glm::vec3(1, 0, 0)) * _randomObjectSpeed[it->first](_randomEngine);
        objectData.materialVariant       = 0;
        objectData.brightness            = randomBrightness(_randomEngine);
        objectData.time2NextTurn         = randomTime2NextTurn(_randomEngine);
        updateData.dynamicObjectData.emplace_back(objectData);
      }
    }
    return updateData.dynamicObjectData.size();
  }
  
  void processInput(std::shared_ptr<pumex::Surface> surface)
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
      measureTime = updateData.measureTime;

    uData.cameraGeographicCoordinates = updateData.cameraGeographicCoordinates;
    uData.cameraDistance              = updateData.cameraDistance;
    uData.cameraPosition              = updateData.cameraPosition;
  }

  void update(std::shared_ptr<pumex::Viewer> viewer, float timeSinceStart, float updateStep)
  {
    // send UpdateData to RenderData
    uint32_t updateIndex = viewer->getUpdateIndex();

    if (_showDynamicRendering)
    {
      tbb::parallel_for
      (
        tbb::blocked_range<size_t>(0, updateData.dynamicObjectData.size()),
        [&](const tbb::blocked_range<size_t>& r)
        {
          for (size_t i = r.begin(); i != r.end(); ++i)
            updateInstance(updateData.dynamicObjectData[i], timeSinceStart, updateStep);
        }
      );
      renderData[updateIndex].dynamicObjectData = updateData.dynamicObjectData;
    }
  }

  void updateInstance(DynamicObjectData& objectData, float timeSinceStart, float updateStep)
  {
    if (objectData.time2NextTurn < 0.0f)
    {
      objectData.kinematic.orientation = glm::angleAxis(_randomRotation(_randomEngine), glm::vec3(0.0f, 0.0f, 1.0f));
      objectData.kinematic.velocity = glm::rotate(objectData.kinematic.orientation, glm::vec3(1, 0, 0)) * _randomObjectSpeed[objectData.typeID](_randomEngine);
      objectData.time2NextTurn = _randomTime2NextTurn(_randomEngine);
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
      objectData.kinematic.velocity    = glm::rotate(objectData.kinematic.orientation, glm::vec3(1, 0, 0)) * _randomObjectSpeed[objectData.typeID](_randomEngine);
      objectData.time2NextTurn         = _randomTime2NextTurn(_randomEngine);
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
    camera.setProjectionMatrix(glm::perspective(glm::radians(60.0f), (float)renderWidth / (float)renderHeight, 0.1f, 100000.0f));
    cameraBuffer->setData(surface.get(), camera);

    pumex::Camera textCamera;
    textCamera.setProjectionMatrix(glm::ortho(0.0f, (float)renderWidth, 0.0f, (float)renderHeight), false);
    textCameraBuffer->setData(surface.get(), textCamera);
  }

  void prepareBuffersForRendering(std::shared_ptr<pumex::Viewer> viewer)
  {
    uint32_t renderIndex = viewer->getRenderIndex();
    const RenderData& rData = renderData[renderIndex];

    float deltaTime  = pumex::inSeconds(viewer->getRenderTimeDelta());
    float renderTime = pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()) + deltaTime;

    if (_showStaticRendering)
    {
      _staticCounterBuffer->invalidateData();
      _staticDrawCommands->invalidateData();
    }

    if (_showDynamicRendering)
    {
      uint32_t maxTypeID = std::max_element(begin(_dynamicTypeIDs), end(_dynamicTypeIDs))->first;
      std::vector<size_t> typeCount(maxTypeID+1);
      std::fill(begin(typeCount), end(typeCount), 0);

      // compute how many instances of each type there is
      for (uint32_t i = 0; i<rData.dynamicObjectData.size(); ++i)
        typeCount[rData.dynamicObjectData[i].typeID]++;

      _dynamicFilterNode->setTypeCount(typeCount);

      std::vector<DynamicInstanceData> dynamicInstanceData;
      for (auto it = begin(rData.dynamicObjectData); it != end(rData.dynamicObjectData); ++it)
        dynamicInstanceData.emplace_back( _dynamicTypeIDs[it->typeID]->update(*it, deltaTime, renderTime) );

      dynamicInstanceBuffer->setData(dynamicInstanceData);
    }
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
  args::ArgumentParser      parser("pumex example : instanced rendering for static and dynamic objects");
  args::HelpFlag            help(parser, "help", "display this help menu", { 'h', "help" });
  args::Flag                enableDebugging(parser, "debug", "enable Vulkan debugging", { 'd' });
  args::Flag                useFullScreen(parser, "fullscreen", "create fullscreen window", { 'f' });
  args::Flag                renderVRwindows(parser, "vrwindows", "create two halfscreen windows for VR", { 'v' });
  args::Flag                render3windows(parser, "three_windows", "render in three windows", { 't' });
  args::Flag                skipStaticRendering(parser, "skip-static", "skip rendering of static objects", { "skip-static" });
  args::Flag                skipDynamicRendering(parser, "skip-dynamic", "skip rendering of dynamic objects", { "skip-dynamic" });
  args::ValueFlag<float>    staticAreaSizeArg(parser, "static-area-size", "size of the area for static rendering", { "static-area-size" }, 2000.0f);
  args::ValueFlag<float>    dynamicAreaSizeArg(parser, "dynamic-area-size", "size of the area for dynamic rendering", { "dynamic-area-size" }, 1000.0f);
  args::ValueFlag<float>    lodModifierArg(parser, "lod-modifier", "LOD range [%]", { "lod-modifier" }, 100.0f);
  args::ValueFlag<float>    densityModifierArg(parser, "density-modifier", "instance density [%]", { "density-modifier" }, 100.0f);
  args::ValueFlag<float>    triangleModifierArg(parser, "triangle-modifier", "instance triangle quantity [%]", { "triangle-modifier" }, 100.0f);
  args::ValueFlag<uint32_t> instancesPerCellArg(parser, "instances-per-cell", "how many static instances per cell", { "instances-per-cell" }, 4096);
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

  bool  showStaticRendering  = !skipStaticRendering;
  bool  showDynamicRendering = !skipDynamicRendering;
  float staticAreaSize       = args::get(staticAreaSizeArg);
  float dynamicAreaSize      = args::get(dynamicAreaSizeArg);
  float lodModifier          = args::get(lodModifierArg) / 100.0f;      // lod distances are multiplied by this parameter
  float densityModifier      = args::get(densityModifierArg) / 100.0f;  // density of objects is multiplied by this parameter
  float triangleModifier     = args::get(triangleModifierArg) / 100.0f; // the number of triangles on geometries is multiplied by this parameter
  uint32_t instancesPerCell  = args::get(instancesPerCellArg);

  LOG_INFO << "Object culling on GPU";
  if (enableDebugging)
    LOG_INFO << " : Vulkan debugging enabled";
  LOG_INFO << std::endl;

  // Below is the definition of Vulkan instance, devices, queues, surfaces, windows, render passes and render threads. All in one place - with all parameters listed
  std::vector<std::string> instanceExtensions;
  std::vector<std::string> requestDebugLayers;
  if (enableDebugging)
    requestDebugLayers.push_back("VK_LAYER_LUNARG_standard_validation");
  pumex::ViewerTraits viewerTraits{ "Gpu cull comparison", instanceExtensions, requestDebugLayers, 60 };
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
    std::vector<std::shared_ptr<pumex::Window>> windows;
    for (const auto& t : windowTraits)
      windows.push_back(pumex::Window::createWindow(t));

    // all created surfaces will use the same device
    std::vector<std::string> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device = viewer->addDevice(0, requestDeviceExtensions);

    pumex::SurfaceTraits surfaceTraits{ 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, VK_PRESENT_MODE_MAILBOX_KHR, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    std::vector<std::shared_ptr<pumex::Surface>> surfaces;
    for (auto& win : windows)
      surfaces.push_back(viewer->addSurface(win, device, surfaceTraits));

    // allocate 32 MB for frame buffers ( actually only depth buffer will be allocated )
    std::shared_ptr<pumex::DeviceMemoryAllocator> frameBufferAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 32 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // alocate 256 MB for uniform and storage buffers
    std::shared_ptr<pumex::DeviceMemoryAllocator> buffersAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 256 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 32 MB for vertex and index buffers
    std::shared_ptr<pumex::DeviceMemoryAllocator> verticesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 32 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    // allocate 4 MB memory for font textures
    std::shared_ptr<pumex::DeviceMemoryAllocator> texturesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 4 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);

    std::vector<pumex::QueueTraits> queueTraits{ { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, 0.75f } };

    std::shared_ptr<pumex::RenderWorkflow> workflow = std::make_shared<pumex::RenderWorkflow>("gpucull_workflow", frameBufferAllocator, queueTraits);
      workflow->addResourceType("depth_samples", false, VK_FORMAT_D32_SFLOAT,    VK_SAMPLE_COUNT_1_BIT, pumex::atDepth,   pumex::AttachmentSize{ pumex::AttachmentSize::SurfaceDependent, glm::vec2(1.0f,1.0f) }, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
      workflow->addResourceType("surface",       true, VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, pumex::atSurface, pumex::AttachmentSize{ pumex::AttachmentSize::SurfaceDependent, glm::vec2(1.0f,1.0f) }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
      workflow->addResourceType("compute_results", false, pumex::RenderWorkflowResourceType::Buffer);

    workflow->addRenderOperation("rendering", pumex::RenderOperation::Graphics);
      workflow->addAttachmentDepthOutput("rendering", "depth_samples", "depth", VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, pumex::loadOpClear(glm::vec2(1.0f, 0.0f)));
      workflow->addAttachmentOutput("rendering", "surface", "color", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, pumex::loadOpClear(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)));

    if (showStaticRendering)
    {
      workflow->addRenderOperation("static_filter", pumex::RenderOperation::Compute);
      workflow->addBufferOutput("static_filter", "compute_results", "static_indirect_results", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
      workflow->addBufferInput("rendering", "compute_results", "static_indirect_results", VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
    }

    if (showDynamicRendering)
    {
      workflow->addRenderOperation("dynamic_filter", pumex::RenderOperation::Compute);
      workflow->addBufferOutput("dynamic_filter", "compute_results", "dynamic_indirect_results", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
      workflow->addBufferInput("rendering", "compute_results", "dynamic_indirect_results", VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
    }

    std::shared_ptr<GpuCullApplicationData> applicationData = std::make_shared<GpuCullApplicationData>(buffersAllocator);

    auto renderingRoot = std::make_shared<pumex::Group>();
    renderingRoot->setName("renderingRoot");
    workflow->setRenderOperationNode("rendering", renderingRoot);

    std::vector<pumex::VertexSemantic>                        vertexSemantic = { { pumex::VertexSemantic::Position, 3 },{ pumex::VertexSemantic::Normal, 3 },{ pumex::VertexSemantic::TexCoord, 3 },{ pumex::VertexSemantic::BoneWeight, 4 },{ pumex::VertexSemantic::BoneIndex, 4 } };
    std::vector<pumex::TextureSemantic>                       textureSemantic = {};
    std::vector<pumex::AssetBufferVertexSemantics>            assetSemantics = { { MAIN_RENDER_MASK, vertexSemantic } };
    std::shared_ptr<pumex::AssetBuffer>                       dynamicAssetBuffer;

    std::shared_ptr<pumex::TextureRegistryNull>               textureRegistryNull = std::make_shared<pumex::TextureRegistryNull>();
    std::shared_ptr<pumex::MaterialRegistry<MaterialGpuCull>> dynamicMaterialRegistry;
    std::shared_ptr<pumex::MaterialSet>                       dynamicMaterialSet;

    std::shared_ptr<pumex::PipelineCache>                     pipelineCache = std::make_shared<pumex::PipelineCache>();
    std::vector<uint32_t>                                     staticTypeIDs;
    std::unordered_map<uint32_t, std::shared_ptr<XXX>>        dynamicTypeIDs;
    std::vector<DynamicObjectData>                            dynamicObjectData;
    std::default_random_engine                                randomEngine;

    auto cameraUbo = std::make_shared<pumex::UniformBuffer>(applicationData->cameraBuffer);

    if (showStaticRendering)
    {
      std::vector<pumex::DescriptorSetLayoutBinding> staticFilterLayoutBindings0 =
      {
        { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT }
      };
      std::vector<pumex::DescriptorSetLayoutBinding> staticFilterLayoutBindings1 =
      {
        { 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT }
      };
      auto staticFilterDescriptorSetLayout0 = std::make_shared<pumex::DescriptorSetLayout>(staticFilterLayoutBindings0);
      auto staticFilterDescriptorSetLayout1 = std::make_shared<pumex::DescriptorSetLayout>(staticFilterLayoutBindings1);
      staticFilterDescriptorSetLayout1->setPreferredPoolSize(256);

      auto staticFilterPipelineLayout  = std::make_shared<pumex::PipelineLayout>();
      staticFilterPipelineLayout->descriptorSetLayouts.push_back(staticFilterDescriptorSetLayout0);
      staticFilterPipelineLayout->descriptorSetLayouts.push_back(staticFilterDescriptorSetLayout1);

      auto staticAssetBuffer      = std::make_shared<pumex::AssetBuffer>(assetSemantics, buffersAllocator, verticesAllocator);
      auto staticMaterialRegistry = std::make_shared<pumex::MaterialRegistry<MaterialGpuCull>>(buffersAllocator);
      auto staticMaterialSet      = std::make_shared<pumex::MaterialSet>(viewer, staticMaterialRegistry, textureRegistryNull, buffersAllocator, textureSemantic);

      applicationData->setupStaticModels(staticAreaSize, lodModifier, triangleModifier, staticAssetBuffer, staticMaterialSet );

      auto staticFilterRoot = std::make_shared<pumex::Group>();
      staticFilterRoot->setName("staticFilterRoot");
      workflow->setRenderOperationNode("static_filter", staticFilterRoot);

      auto staticFilterPipeline = std::make_shared<pumex::ComputePipeline>(pipelineCache, staticFilterPipelineLayout);
      staticFilterPipeline->setName("staticFilterPipeline");
      staticFilterPipeline->shaderStage = { VK_SHADER_STAGE_COMPUTE_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/gpucull_static_filter_instances.comp.spv")), "main" };
      staticFilterRoot->addChild(staticFilterPipeline);
      staticFilterPipeline->useSecondaryBuffer();


      auto staticCounterBuffer = std::make_shared<pumex::Buffer<uint32_t>>(std::make_shared<uint32_t>(0), buffersAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pumex::pbPerSurface, pumex::swOnce);
      auto staticCounterSbo    = std::make_shared<pumex::StorageBuffer>(staticCounterBuffer);

      auto staticResultsIndexBuffer = std::make_shared<pumex::Buffer<std::vector<uint32_t>>>(std::make_shared<std::vector<uint32_t>>(), buffersAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pumex::pbPerSurface, pumex::swForEachImage);
      auto staticResultsIndexSbo = std::make_shared<pumex::StorageBuffer>(staticResultsIndexBuffer);

      auto staticResultsBuffer = std::make_shared<pumex::Buffer<std::vector<StaticInstanceData>>>(std::make_shared<std::vector<StaticInstanceData>>(), buffersAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pumex::pbPerSurface, pumex::swForEachImage);
      auto staticResultsSbo = std::make_shared<pumex::StorageBuffer>(staticResultsBuffer);
      workflow->associateMemoryObject("static_indirect_results", staticResultsBuffer);

      auto staticAssetBufferFilterNode = std::make_shared<pumex::AssetBufferFilterNode>(staticAssetBuffer, buffersAllocator);
      staticAssetBufferFilterNode->setEventResizeOutputs(std::bind(resizeStaticOutputBuffers, staticResultsBuffer, staticResultsIndexBuffer, std::placeholders::_1, std::placeholders::_2));
      staticAssetBufferFilterNode->setName("staticAssetBufferFilterNode");
      staticFilterPipeline->addChild(staticAssetBufferFilterNode);

      std::shared_ptr<pumex::Node> instanceTree = applicationData->setupStaticInstances(staticAreaSize, densityModifier, instancesPerCell, staticAssetBufferFilterNode, buffersAllocator, staticFilterDescriptorSetLayout1);
      applicationData->setupStaticBuffers(staticCounterBuffer, staticAssetBufferFilterNode->getDrawIndexedIndirectBuffer(MAIN_RENDER_MASK));
      staticAssetBufferFilterNode->addChild(instanceTree);

      auto staticFilterDescriptorSet0 = std::make_shared<pumex::DescriptorSet>(staticFilterDescriptorSetLayout0);
      staticFilterDescriptorSet0->setDescriptor(0, cameraUbo);
      staticFilterDescriptorSet0->setDescriptor(1, std::make_shared<pumex::StorageBuffer>(staticAssetBuffer->getTypeBuffer(MAIN_RENDER_MASK)));
      staticFilterDescriptorSet0->setDescriptor(2, std::make_shared<pumex::StorageBuffer>(staticAssetBuffer->getLodBuffer(MAIN_RENDER_MASK)));
      staticFilterDescriptorSet0->setDescriptor(3, std::make_shared<pumex::StorageBuffer>(staticAssetBufferFilterNode->getDrawIndexedIndirectBuffer(MAIN_RENDER_MASK)));
      staticFilterDescriptorSet0->setDescriptor(4, staticResultsSbo);
      staticFilterDescriptorSet0->setDescriptor(5, staticResultsIndexSbo);
      staticFilterDescriptorSet0->setDescriptor(6, staticCounterSbo);
      instanceTree->setDescriptorSet(0, staticFilterDescriptorSet0);

      // setup static rendering
      std::vector<pumex::DescriptorSetLayoutBinding> staticRenderLayoutBindings =
      {
        { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT }
      };
      auto staticRenderDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(staticRenderLayoutBindings);
      auto staticRenderPipelineLayout      = std::make_shared<pumex::PipelineLayout>();
      staticRenderPipelineLayout->descriptorSetLayouts.push_back(staticRenderDescriptorSetLayout);

      auto staticRenderPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, staticRenderPipelineLayout);
      staticRenderPipeline->shaderStages =
      {
        { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/gpucull_static_render.vert.spv")), "main" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/gpucull_static_render.frag.spv")), "main" }
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
      renderingRoot->addChild(staticRenderPipeline);

      auto staticAssetBufferNode = std::make_shared<pumex::AssetBufferNode>(staticAssetBuffer, staticMaterialSet, MAIN_RENDER_MASK, 0);
      staticAssetBufferNode->setName("staticAssetBufferNode");
      staticRenderPipeline->addChild(staticAssetBufferNode);

      auto staticAssetBufferDrawIndirect = std::make_shared<pumex::AssetBufferIndirectDrawObjects>(staticAssetBufferFilterNode, MAIN_RENDER_MASK);
      staticAssetBufferDrawIndirect->setName("staticAssetBufferDrawIndirect");
      staticAssetBufferNode->addChild(staticAssetBufferDrawIndirect);

      auto staticRenderDescriptorSet = std::make_shared<pumex::DescriptorSet>(staticRenderDescriptorSetLayout);
      staticRenderDescriptorSet->setDescriptor(0, cameraUbo);
      staticRenderDescriptorSet->setDescriptor(1, staticResultsIndexSbo);
      staticRenderDescriptorSet->setDescriptor(2, staticResultsSbo);
      staticRenderDescriptorSet->setDescriptor(3, std::make_shared<pumex::StorageBuffer>(staticMaterialSet->typeDefinitionBuffer));
      staticRenderDescriptorSet->setDescriptor(4, std::make_shared<pumex::StorageBuffer>(staticMaterialSet->materialVariantBuffer));
      staticRenderDescriptorSet->setDescriptor(5, std::make_shared<pumex::StorageBuffer>(staticMaterialRegistry->materialDefinitionBuffer));
      staticAssetBufferDrawIndirect->setDescriptorSet(0, staticRenderDescriptorSet);
    }

    if (showDynamicRendering)
    {
      std::vector<pumex::DescriptorSetLayoutBinding> dynamicFilterLayoutBindings =
      {
        { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT }
      };
      auto dynamicFilterDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(dynamicFilterLayoutBindings);

      auto dynamicFilterPipelineLayout = std::make_shared<pumex::PipelineLayout>();
      dynamicFilterPipelineLayout->descriptorSetLayouts.push_back(dynamicFilterDescriptorSetLayout);

      dynamicAssetBuffer      = std::make_shared<pumex::AssetBuffer>(assetSemantics, buffersAllocator, verticesAllocator);
      dynamicMaterialRegistry = std::make_shared<pumex::MaterialRegistry<MaterialGpuCull>>(buffersAllocator);
      dynamicMaterialSet      = std::make_shared<pumex::MaterialSet>(viewer, dynamicMaterialRegistry, textureRegistryNull, buffersAllocator, textureSemantic);

      applicationData->setupDynamicModels(lodModifier, triangleModifier, dynamicAssetBuffer, dynamicMaterialSet);

      auto dynamicFilterRoot = std::make_shared<pumex::Group>();
      dynamicFilterRoot->setName("staticFilterRoot");
      workflow->setRenderOperationNode("dynamic_filter", dynamicFilterRoot);

      auto dynamicFilterPipeline = std::make_shared<pumex::ComputePipeline>(pipelineCache, dynamicFilterPipelineLayout);
      dynamicFilterPipeline->shaderStage = { VK_SHADER_STAGE_COMPUTE_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/gpucull_dynamic_filter_instances.comp.spv")), "main" };
      
      dynamicFilterRoot->addChild(dynamicFilterPipeline);

      auto dynamicResultsBuffer = std::make_shared<pumex::Buffer<std::vector<uint32_t>>>(std::make_shared<std::vector<uint32_t>>(), buffersAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pumex::pbPerSurface, pumex::swForEachImage);
      auto dynamicResultsSbo = std::make_shared<pumex::StorageBuffer>(dynamicResultsBuffer);
      workflow->associateMemoryObject("dynamic_indirect_results", dynamicResultsBuffer);

      auto dynamicAssetBufferFilterNode = std::make_shared<pumex::AssetBufferFilterNode>(dynamicAssetBuffer, buffersAllocator);
      dynamicAssetBufferFilterNode->setName("dynamicAssetBufferFilterNode");
      dynamicFilterPipeline->addChild(dynamicAssetBufferFilterNode);

      uint32_t instanceCount = applicationData->setupDynamicInstances(dynamicAreaSize, densityModifier, dynamicAssetBufferFilterNode);

      auto dynamicDispatchNode = std::make_shared<pumex::DispatchNode>(instanceCount / 16 + ((instanceCount % 16 > 0) ? 1 : 0), 1, 1);
      dynamicDispatchNode->setName("dynamicDispatchNode");
      dynamicAssetBufferFilterNode->addChild(dynamicDispatchNode);
      dynamicAssetBufferFilterNode->setEventResizeOutputs(std::bind(resizeDynamicOutputBuffers, dynamicResultsBuffer, dynamicDispatchNode, std::placeholders::_1, std::placeholders::_2));

      auto dynamicFilterDescriptorSet = std::make_shared<pumex::DescriptorSet>(dynamicFilterDescriptorSetLayout);
      dynamicFilterDescriptorSet->setDescriptor(0, cameraUbo);
      dynamicFilterDescriptorSet->setDescriptor(1, std::make_shared<pumex::StorageBuffer>(dynamicAssetBuffer->getTypeBuffer(MAIN_RENDER_MASK)));
      dynamicFilterDescriptorSet->setDescriptor(2, std::make_shared<pumex::StorageBuffer>(dynamicAssetBuffer->getLodBuffer(MAIN_RENDER_MASK)));
      dynamicFilterDescriptorSet->setDescriptor(3, std::make_shared<pumex::StorageBuffer>(applicationData->dynamicInstanceBuffer));
      dynamicFilterDescriptorSet->setDescriptor(4, std::make_shared<pumex::StorageBuffer>(dynamicAssetBufferFilterNode->getDrawIndexedIndirectBuffer(MAIN_RENDER_MASK)));
      dynamicFilterDescriptorSet->setDescriptor(5, dynamicResultsSbo);
      dynamicDispatchNode->setDescriptorSet(0, dynamicFilterDescriptorSet);

      std::vector<pumex::DescriptorSetLayoutBinding> dynamicRenderLayoutBindings =
      {
        { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
        { 5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT }
      };
      auto dynamicRenderDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(dynamicRenderLayoutBindings);
      auto dynamicRenderPipelineLayout      = std::make_shared<pumex::PipelineLayout>();
      dynamicRenderPipelineLayout->descriptorSetLayouts.push_back(dynamicRenderDescriptorSetLayout);

      auto dynamicRenderPipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, dynamicRenderPipelineLayout);
      dynamicRenderPipeline->shaderStages =
      {
        { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/gpucull_dynamic_render.vert.spv")), "main" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/gpucull_dynamic_render.frag.spv")), "main" }
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
      renderingRoot->addChild(dynamicRenderPipeline);

      auto dynamicAssetBufferNode = std::make_shared<pumex::AssetBufferNode>(dynamicAssetBuffer, dynamicMaterialSet, MAIN_RENDER_MASK, 0);
      dynamicAssetBufferNode->setName("dynamicAssetBufferNode");
      dynamicRenderPipeline->addChild(dynamicAssetBufferNode);

      auto dynamicAssetBufferDrawIndirect = std::make_shared<pumex::AssetBufferIndirectDrawObjects>(dynamicAssetBufferFilterNode, MAIN_RENDER_MASK);
      dynamicAssetBufferDrawIndirect->setName("dynamicAssetBufferDrawIndirect");
      dynamicAssetBufferNode->addChild(dynamicAssetBufferDrawIndirect);

      auto dynamicRenderDescriptorSet = std::make_shared<pumex::DescriptorSet>(dynamicRenderDescriptorSetLayout);
      dynamicRenderDescriptorSet->setDescriptor(0, cameraUbo);
      dynamicRenderDescriptorSet->setDescriptor(1, std::make_shared<pumex::StorageBuffer>(applicationData->dynamicInstanceBuffer));
      dynamicRenderDescriptorSet->setDescriptor(2, dynamicResultsSbo);
      dynamicRenderDescriptorSet->setDescriptor(3, std::make_shared<pumex::StorageBuffer>(dynamicMaterialSet->typeDefinitionBuffer));
      dynamicRenderDescriptorSet->setDescriptor(4, std::make_shared<pumex::StorageBuffer>(dynamicMaterialSet->materialVariantBuffer));
      dynamicRenderDescriptorSet->setDescriptor(5, std::make_shared<pumex::StorageBuffer>(dynamicMaterialRegistry->materialDefinitionBuffer));
      dynamicAssetBufferDrawIndirect->setDescriptorSet(0, dynamicRenderDescriptorSet);
    }

    std::string fullFontFileName = viewer->getFullFilePath("fonts/DejaVuSans.ttf");
    auto fontDefault = std::make_shared<pumex::Font>(fullFontFileName, glm::uvec2(1024, 1024), 24, texturesAllocator);
    auto textDefault = std::make_shared<pumex::Text>(fontDefault, buffersAllocator);

    auto fontSmall   = std::make_shared<pumex::Font>(fullFontFileName, glm::uvec2(512, 512), 16, texturesAllocator);
    auto textSmall   = std::make_shared<pumex::Text>(fontSmall, buffersAllocator);

    // building text pipeline layout
    std::vector<pumex::DescriptorSetLayoutBinding> textLayoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_GEOMETRY_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    auto textDescriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(textLayoutBindings);
    auto textPipelineLayout      = std::make_shared<pumex::PipelineLayout>();
    textPipelineLayout->descriptorSetLayouts.push_back(textDescriptorSetLayout);
    auto textPipeline            = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, textPipelineLayout);
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
      { VK_SHADER_STAGE_VERTEX_BIT,   std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/text_draw.vert.spv")), "main" },
      { VK_SHADER_STAGE_GEOMETRY_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/text_draw.geom.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/text_draw.frag.spv")), "main" }
    };
    textPipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    renderingRoot->addChild(textPipeline);

    textPipeline->addChild(textDefault);
    textPipeline->addChild(textSmall);

    auto fontImageView = std::make_shared<pumex::ImageView>(fontDefault->fontMemoryImage, fontDefault->fontMemoryImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D);
    auto fontSampler = std::make_shared<pumex::Sampler>(pumex::SamplerTraits());

    auto textCameraUbo = std::make_shared<pumex::UniformBuffer>(applicationData->textCameraBuffer);

    auto textDescriptorSet = std::make_shared<pumex::DescriptorSet>(textDescriptorSetLayout);
    textDescriptorSet->setDescriptor(0, textCameraUbo);
    textDescriptorSet->setDescriptor(1, std::make_shared<pumex::CombinedImageSampler>(fontImageView, fontSampler));
    textDefault->setDescriptorSet(0, textDescriptorSet);

    auto smallFontImageView = std::make_shared<pumex::ImageView>(fontSmall->fontMemoryImage, fontSmall->fontMemoryImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D);

    auto textDescriptorSetSmall = std::make_shared<pumex::DescriptorSet>(textDescriptorSetLayout);
    textDescriptorSetSmall->setDescriptor(0, textCameraUbo);
    textDescriptorSetSmall->setDescriptor(1, std::make_shared<pumex::CombinedImageSampler>(smallFontImageView, fontSampler));
    textSmall->setDescriptorSet(0, textDescriptorSetSmall);

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

    // connecting workflow to all surfaces
    std::shared_ptr<pumex::SingleQueueWorkflowCompiler> workflowCompiler = std::make_shared<pumex::SingleQueueWorkflowCompiler>();
    for (auto& surf : surfaces)
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
      for (auto& surf : surfaces)
        applicationData->processInput(surf);
      auto updateBeginTime = applicationData->setTime(1010, inputBeginTime);
      applicationData->update(viewer, pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()), pumex::inSeconds(viewer->getUpdateDuration()));
      applicationData->setTime(1020, updateBeginTime);
    });

    tbb::flow::make_edge(viewer->opStartUpdateGraph, update);
    tbb::flow::make_edge(update, viewer->opEndUpdateGraph);

    // set render callbacks to application data
    viewer->setEventRenderStart(std::bind(&GpuCullApplicationData::prepareBuffersForRendering, applicationData, std::placeholders::_1));
    for (auto& surf : surfaces)
      surf->setEventSurfaceRenderStart(std::bind(&GpuCullApplicationData::prepareCameraForRendering, applicationData, std::placeholders::_1));

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
