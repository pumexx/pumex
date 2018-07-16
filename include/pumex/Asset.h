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

#pragma once
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <pumex/Export.h>
#include <pumex/BoundingBox.h>

namespace pumex
{

const glm::mat4 mat4unity = glm::mat4();

class Device;
class Viewer;

// Class representing a tree of bones to animate the asset
// Two rules are mandatory: 
// - parents in bone vector must be defined before their children
// - there must be at least one bone in each asset ( the root )
class PUMEX_EXPORT Skeleton
{
public:
  explicit Skeleton() = default;

  // TODO - add some interface maybe ?
  struct Bone
  {
    uint32_t   parentIndex    = std::numeric_limits<uint32_t>::max();
    uint32_t   childrenOffset = 0;
    uint32_t   childrenSize   = 0;
    uint32_t   boneTag = 0; // boneTag = 1 .. there are animated bones down the hierarchy
    glm::mat4  localTransformation; // for nodes ( dummies, not animated parents )
    glm::mat4  offsetMatrix;        // for bones
  };
  std::vector<Bone>                  bones;
  std::vector<uint32_t>              children;
  glm::mat4                          invGlobalTransform;
  std::string                        name;
  std::vector<std::string>           boneNames;
  std::map<std::string, std::size_t> invBoneNames;
  
  void refreshChildren();
};

// struct defining contents of a single vertex
struct PUMEX_EXPORT VertexSemantic
{
  enum Type { Position, Normal, TexCoord, Color, Tangent, Bitangent, BoneIndex, BoneWeight };

  VertexSemantic(const Type& t, uint32_t s)
    : type{t}, size{s}
  {
  }
  Type     type;
  uint32_t size;

  VkFormat getVertexFormat() const;
};

inline bool operator==(const VertexSemantic& lhs, const VertexSemantic& rhs)
{
  return (lhs.type == rhs.type) && (lhs.size == rhs.size);
}

PUMEX_EXPORT uint32_t calcVertexSize(const std::vector<VertexSemantic>& layout);
PUMEX_EXPORT uint32_t calcPrimitiveSize(VkPrimitiveTopology topology);

// helper class to deal with vertices having different vertex semantics
class PUMEX_EXPORT VertexAccumulator
{
public:
  explicit VertexAccumulator(const std::vector<VertexSemantic>& semantic);
  void set(VertexSemantic::Type semanticType, uint32_t channel, float val0);
  void set(VertexSemantic::Type semanticType, uint32_t channel, float val0, float val1);
  void set(VertexSemantic::Type semanticType, uint32_t channel, float val0, float val1, float val2);
  void set(VertexSemantic::Type semanticType, uint32_t channel, float val0, float val1, float val2, float val3);

  void set(VertexSemantic::Type semanticType, float val0);
  void set(VertexSemantic::Type semanticType, float val0, float val1);
  void set(VertexSemantic::Type semanticType, float val0, float val1, float val2);
  void set(VertexSemantic::Type semanticType, float val0, float val1, float val2, float val3);

  void reset();

  glm::vec4 getPosition() const;
  glm::vec4 getNormal() const;
  glm::vec4 getTexCoord(uint32_t channel) const;
  glm::vec4 getColor(uint32_t channel) const;
  glm::vec4 getTangent() const;
  glm::vec4 getBitangent() const;
  glm::vec4 getBoneIndex() const;
  glm::vec4 getBoneWeight() const;


  std::vector<float> values;
protected:
  inline uint32_t getOffset(VertexSemantic::Type semanticType, uint32_t channel) const;
  std::vector<VertexSemantic> semantic;

  std::vector<uint32_t> positionOffset;
  std::vector<uint32_t> normalOffset;
  std::vector<uint32_t> texCoordOffset;
  std::vector<uint32_t> colorOffset;
  std::vector<uint32_t> tangentOffset;
  std::vector<uint32_t> bitangentOffset;
  std::vector<uint32_t> boneIndexOffset;
  std::vector<uint32_t> boneWeightOffset;

  std::vector<float>    valuesReset;
};

// basic class for storing vertices and indices - subject of vkCmdDraw* commands
struct PUMEX_EXPORT Geometry
{
  std::string                 name;
  VkPrimitiveTopology         topology      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  std::vector<VertexSemantic> semantic;
  uint32_t                    materialIndex = 0;
  uint32_t                    renderMask    = 1;

  std::vector<float>          vertices;
  std::vector<uint32_t>       indices;

  inline VkDeviceSize getVertexCount() const; // number of vertices
  inline VkDeviceSize getVertexSize() const; // size in bytes
  inline VkDeviceSize getIndexCount() const; // number of indices
  inline VkDeviceSize getIndexSize() const; // size in bytes
  inline VkDeviceSize getPrimitiveCount() const; // how many faces

  void pushVertex(const VertexAccumulator& vertexAccumulator);
  void setVertex(uint32_t position, const VertexAccumulator& vertexAccumulator);
  void getVertex(uint32_t position, VertexAccumulator& vertexAccumulator);
};

// class storing properties and texture names of materials
// For now properties of a material are named after assimp library for convenience
struct PUMEX_EXPORT Material
{
  std::string name;
  std::unordered_map<uint32_t,std::string>  textures;
  std::unordered_map<std::string, glm::vec4> properties;

  glm::vec4 getProperty(const std::string& name, glm::vec4 defaultValue) const;
};

template<typename T>
struct TimeLine
{
  TimeLine(float t, const T& v)
    : time{t}, value{v}
  {
  }
  float time;
  T     value;
};

template<typename T>
inline bool operator<(const TimeLine<T>& lhs, const TimeLine<T>& rhs)
{
  return lhs.time<rhs.time;
}

// class storing information about Asset animations
struct PUMEX_EXPORT Animation
{
  struct Channel
  {
    enum State { CLAMP, REPEAT };
    std::vector<TimeLine<glm::vec3>> position;
    std::vector<TimeLine<glm::quat>> rotation;
    std::vector<TimeLine<glm::vec3>> scale;

    float positionTimeBegin = 0.0f;
    float positionTimeEnd   = 0.0f;
    float rotationTimeBegin = 0.0f;
    float rotationTimeEnd   = 0.0f;
    float scaleTimeBegin    = 0.0f;
    float scaleTimeEnd      = 0.0f;

    void calcBeginEndTimes();
    float beginTime() const;
    float endTime() const;

    glm::mat4 calculateTransform(float time, Channel::State before, Channel::State after) const;
  };

  void calculateLocalTransforms(float time, glm::mat4* data, uint32_t size) const;

  std::string                        name;
  std::vector<Channel>               channels;
  std::vector<Channel::State>        channelBefore;
  std::vector<Channel::State>        channelAfter;
  std::vector<std::string>           channelNames;  // channel name = bone name
  std::map<std::string, std::size_t> invChannelNames;
};

// Main class for storing information about an asset loaded from file ( by assimp or custom created loaders )
// TODO : should we add lights in some form here ?
class PUMEX_EXPORT Asset
{
public:
  Skeleton               skeleton;
  std::vector<Geometry>  geometries;
  std::vector<Material>  materials;
  std::vector<Animation> animations;
  std::string            fileName;
};

// this is temporary solution for asset loading
class PUMEX_EXPORT AssetLoader
{
public:
  virtual std::shared_ptr<Asset> load(std::shared_ptr<Viewer> viewer, const std::string& fileName, bool animationOnly = false, const std::vector<VertexSemantic>& requiredSemantic = std::vector<VertexSemantic>()) = 0;
};

uint32_t VertexAccumulator::getOffset(VertexSemantic::Type semanticType, uint32_t channel) const
{
  switch (semanticType)
  {
  case VertexSemantic::Position:
    if (positionOffset.size() > channel)
      return positionOffset[channel];
  case VertexSemantic::Normal:
    if (normalOffset.size() > channel)
      return normalOffset[channel];
  case VertexSemantic::TexCoord:
    if (texCoordOffset.size() > channel)
      return texCoordOffset[channel];
  case VertexSemantic::Color:
    if (colorOffset.size() > channel)
      return colorOffset[channel];
  case VertexSemantic::Tangent:
    if (tangentOffset.size() > channel)
      return tangentOffset[channel];
  case VertexSemantic::Bitangent:
    if (bitangentOffset.size() > channel)
      return bitangentOffset[channel];
  case VertexSemantic::BoneIndex:
    if (boneIndexOffset.size() > channel)
      return boneIndexOffset[channel];
  case VertexSemantic::BoneWeight:
    if (boneWeightOffset.size() > channel)
      return boneWeightOffset[channel];
  }
  return std::numeric_limits<uint32_t>::max();
}

VkDeviceSize Geometry::getVertexCount() const    { return vertices.size() / calcVertexSize(semantic); }
VkDeviceSize Geometry::getVertexSize() const     { return vertices.size() * sizeof(float); }
VkDeviceSize Geometry::getIndexCount() const     { return indices.size(); }
VkDeviceSize Geometry::getIndexSize() const      { return indices.size() * sizeof(uint32_t); }
VkDeviceSize Geometry::getPrimitiveCount() const { return indices.size() / calcPrimitiveSize(topology); }

// convert vertices from one semantic to another
PUMEX_EXPORT void copyAndConvertVertices(std::vector<float>& targetBuffer, const std::vector<VertexSemantic>& targetSemantic, const std::vector<float>& sourceBuffer, const std::vector<VertexSemantic>& sourceSemantic);
// transform vertices using matrix
PUMEX_EXPORT void transformGeometry(const glm::mat4& matrix , Geometry& geometry);
// merge two assets into one
PUMEX_EXPORT void mergeAsset(Asset& parentAsset, uint32_t parentBone, Asset& childAsset);

// calculate matrices for reset position ( T-pose for people )
PUMEX_EXPORT std::vector<glm::mat4> calculateResetPosition(const Asset& asset);

// calculate bounding box taking asset tree and its all geometries into account ( animation matrices set to initial position )
PUMEX_EXPORT BoundingBox calculateBoundingBox(const Asset& asset, uint32_t renderMask);
// calculate bounding box taking only geometry vertices into account
PUMEX_EXPORT BoundingBox calculateBoundingBox(const Geometry& geometry, const std::vector<glm::mat4>& bones);
// calculate bounding box taking animation into account
PUMEX_EXPORT BoundingBox calculateBoundingBox(const Skeleton& skeleton, const Animation& animation, bool addFictionalLeaves);

// given time belongs to <v[index]..v[index+1])
template<typename T>
inline uint32_t binarySearchIndex(const TimeLine<T>* values, uint32_t size, float time)
{
  uint32_t begin = 0;
  uint32_t end = size;
  uint32_t mid = (end + begin) >> 1;
  while (mid != begin)
  {
    if (values[mid].time > time)
      end = mid;
    else
      begin = mid;
    mid = (end + begin) >> 1;
  }
  return begin;
}

template<typename T>
inline float tBeginTime(const std::vector<TimeLine<T>>& values)
{
  if (values.empty())
    return 0.0f;
  return values.front().time;
}

template<typename T>
inline float tEndTime(const std::vector<TimeLine<T>>& values)
{
  if (values.empty())
    return 0.0f;
  return values.back().time;
}

inline float PUMEX_EXPORT calculateAnimationTime( float time, float begin, float end, Animation::Channel::State before, Animation::Channel::State after)
{
  float duration = end - begin;
  if (duration == 0.0f)
    return 0.0f;
  float normTime, fraction;

  if (time < begin)
  {
    switch (before)
    {
    case Animation::Channel::State::CLAMP:
      time     = begin;
      break;
    case Animation::Channel::State::REPEAT:
      normTime = (time - begin) / duration;
      fraction = normTime - floor(normTime);
      time     = begin + fraction * duration;
      break;
    }
  }
  else if (time > end)
  {
    switch (after)
    {
    case Animation::Channel::State::CLAMP:
      time     = end;
      break;
    case Animation::Channel::State::REPEAT:
      normTime = (time - begin) / duration;
      fraction = normTime - floor(normTime);
      time     = begin + fraction * duration;
      break;
    }
  }
  return time;
}

// linear interpolation
template<typename T>
inline T mix(const TimeLine<T>* values, const uint32_t size, float time)
{
  uint32_t i = binarySearchIndex(values, size, time);
  float    a = (time - values[i].time) / (values[(i+1)%size].time - values[i].time);
  return   glm::mix(values[i].value, values[(i+1)%size].value, a);
}

// spherical interpolation
template<typename T>
inline T slerp(const TimeLine<T>* values, const uint32_t size, float time)
{
  uint32_t i = binarySearchIndex(values, size, time);
  float    a = (time - values[i].time) / (values[(i+1)%size].time - values[i].time);
  return   glm::slerp(values[i].value, values[(i+1)%size].value, a);
}

}

