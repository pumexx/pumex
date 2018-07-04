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

#include <pumex/Asset.h>
#include <set>
#include <queue>
#include <algorithm>
#include <iterator>
#include <pumex/Device.h>
#include <pumex/Surface.h>
#include <pumex/utils/Log.h>
#include <pumex/utils/Buffer.h>
#include <glm/gtc/matrix_transform.hpp>

namespace pumex
{

void Skeleton::refreshChildren()
{
  children.resize(0);
  for (uint32_t i = 0; i < bones.size(); ++i)
  {
    bones[i].childrenOffset = static_cast<uint32_t>(children.size());
    for (uint32_t j = i + 1; j < bones.size(); ++j)
    {
      if (bones[j].parentIndex == i)
        children.push_back(j);
    }
    bones[i].childrenSize = children.size() - bones[i].childrenOffset;
  }
}

VkFormat VertexSemantic::getVertexFormat() const
{
  switch (size)
  {
  case 1:
    return VK_FORMAT_R32_SFLOAT;
  case 2:
    return VK_FORMAT_R32G32_SFLOAT;
  case 3:
    return VK_FORMAT_R32G32B32_SFLOAT;
  case 4:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  }
  return VK_FORMAT_UNDEFINED;
}

uint32_t calcVertexSize(const std::vector<VertexSemantic>& layout)
{
  uint32_t result = 0;
  for ( const auto& l : layout )
    result += l.size;
  return result;
}

uint32_t calcPrimitiveSize(VkPrimitiveTopology topology)
{
  switch (topology)
  {
  case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
    return 1;
  case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
    return 2;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
    return 3;
  default:
    return 3;
  }
}

VertexAccumulator::VertexAccumulator(const std::vector<VertexSemantic>& s)
  : semantic(s)
{
  uint32_t currentOffset = 0;
  for (const auto& t : semantic)
  {
    switch (t.type)
    {
    case VertexSemantic::Position:
      positionOffset.push_back(currentOffset);
      for (uint32_t i=0; i<t.size; ++i)
        valuesReset.push_back(0.0f);
      break;
    case VertexSemantic::Normal:
      normalOffset.push_back(currentOffset);
      for (uint32_t i = 0; i<t.size-1; ++i)
        valuesReset.push_back(0.0f);
      valuesReset.push_back(1.0f);
      break;
    case VertexSemantic::TexCoord:
      texCoordOffset.push_back(currentOffset);
      for (uint32_t i = 0; i<t.size; ++i)
        valuesReset.push_back(0.0f);
      break;
    case VertexSemantic::Color:
      colorOffset.push_back(currentOffset);
      for (uint32_t i = 0; i<t.size; ++i)
        valuesReset.push_back(1.0f);
      break;
    case VertexSemantic::Tangent:
      tangentOffset.push_back(currentOffset);
      valuesReset.push_back(1.0f);
      valuesReset.push_back(0.0f);
      valuesReset.push_back(0.0f);
      break;
    case VertexSemantic::Bitangent:
      bitangentOffset.push_back(currentOffset);
      valuesReset.push_back(0.0f);
      valuesReset.push_back(1.0f);
      valuesReset.push_back(0.0f);
      break;
    case VertexSemantic::BoneIndex:
      boneIndexOffset.push_back(currentOffset);
      valuesReset.push_back(0.0f);
      for (uint32_t i = 1; i<t.size; ++i)
        valuesReset.push_back(0.0f);
      break;
    case VertexSemantic::BoneWeight:
      boneWeightOffset.push_back(currentOffset);
      valuesReset.push_back(1.0f);
      for (uint32_t i = 1; i<t.size; ++i)
        valuesReset.push_back(0.0f);
      break;
    }
    currentOffset += t.size;
  }
  reset();
}

void VertexAccumulator::set(VertexSemantic::Type semanticType, uint32_t channel, float val0)
{
  uint32_t offset  = getOffset(semanticType, channel);
  if (offset==UINT32_MAX)
    return;
  values[offset]   = val0;
}

void VertexAccumulator::set(VertexSemantic::Type semanticType, uint32_t channel, float val0, float val1)
{
  uint32_t offset  = getOffset(semanticType, channel);
  if (offset == UINT32_MAX)
    return;
  values[offset] = val0;
  values[offset+1] = val1;
}

void VertexAccumulator::set(VertexSemantic::Type semanticType, uint32_t channel, float val0, float val1, float val2)
{
  uint32_t offset  = getOffset(semanticType, channel);
  if (offset == UINT32_MAX)
    return;
  values[offset] = val0;
  values[offset+1] = val1;
  values[offset+2] = val2;
}

void VertexAccumulator::set(VertexSemantic::Type semanticType, uint32_t channel, float val0, float val1, float val2, float val3)
{
  uint32_t offset  = getOffset(semanticType,channel);
  if (offset == UINT32_MAX)
    return;
  values[offset] = val0;
  values[offset+1] = val1;
  values[offset+2] = val2;
  values[offset+3] = val3;
}

void VertexAccumulator::set(VertexSemantic::Type semanticType, float val0)
{
  uint32_t offset  = getOffset(semanticType, 0);
  if (offset == UINT32_MAX)
    return;
  values[offset] = val0;
}

void VertexAccumulator::set(VertexSemantic::Type semanticType, float val0, float val1)
{
  uint32_t offset  = getOffset(semanticType, 0);
  if (offset == UINT32_MAX)
    return;
  values[offset] = val0;
  values[offset+1] = val1;
}

void VertexAccumulator::set(VertexSemantic::Type semanticType, float val0, float val1, float val2)
{
  uint32_t offset  = getOffset(semanticType, 0);
  if (offset == UINT32_MAX)
    return;
  values[offset] = val0;
  values[offset+1] = val1;
  values[offset+2] = val2;
}

void VertexAccumulator::set(VertexSemantic::Type semanticType, float val0, float val1, float val2, float val3)
{
  uint32_t offset  = getOffset(semanticType, 0);
  if (offset == UINT32_MAX)
    return;
  values[offset] = val0;
  values[offset+1] = val1;
  values[offset+2] = val2;
  values[offset+3] = val3;
}

void VertexAccumulator::reset()
{
  values = valuesReset;
}

glm::vec4 VertexAccumulator::getPosition() const
{
  if (positionOffset.empty())
    return glm::vec4(0.0,0.0,0.0,1.0);
  return glm::vec4(values[positionOffset[0]+0], values[positionOffset[0]+1], values[positionOffset[0]+2], 1.0f);
}

glm::vec4 VertexAccumulator::getNormal() const
{
  if (normalOffset.empty())
    return glm::vec4(0.0, 0.0, 1.0, 1.0);
  return glm::vec4(values[normalOffset[0] + 0], values[normalOffset[0] + 1], values[normalOffset[0] + 2], 1.0f);
}

glm::vec4 VertexAccumulator::getTexCoord(uint32_t channel) const
{
  if (texCoordOffset.empty())
    return glm::vec4(0.0, 0.0, 0.0, 1.0);
  // FIXME - no bounds checking - may end badly...
  return glm::vec4(values[texCoordOffset[channel] + 0], values[texCoordOffset[channel] + 1], values[texCoordOffset[channel] + 2], values[texCoordOffset[channel] + 3]);
}

glm::vec4 VertexAccumulator::getColor(uint32_t channel) const
{
  if (colorOffset.empty())
    return glm::vec4(0.0, 0.0, 0.0, 1.0);
  // FIXME - no bounds checking - may end badly...
  return glm::vec4(values[colorOffset[channel] + 0], values[colorOffset[channel] + 1], values[colorOffset[channel] + 2], values[colorOffset[channel] + 3]);
}

glm::vec4 VertexAccumulator::getTangent() const
{
  if (tangentOffset.empty())
    return glm::vec4(1.0, 0.0, 0.0, 1.0);
  return glm::vec4(values[tangentOffset[0] + 0], values[tangentOffset[0] + 1], values[tangentOffset[0] + 2], 1.0f);
}

glm::vec4 VertexAccumulator::getBitangent() const
{
  if (bitangentOffset.empty())
    return glm::vec4(0.0, 1.0, 0.0, 1.0);
  return glm::vec4(values[bitangentOffset[0] + 0], values[bitangentOffset[0] + 1], values[bitangentOffset[0] + 2], 1.0f);
}

glm::vec4 VertexAccumulator::getBoneIndex() const
{
  if (boneIndexOffset.empty())
    return glm::vec4(0.0, 0.0, 0.0, 0.0);
  return glm::vec4(values[boneIndexOffset[0] + 0], values[boneIndexOffset[0] + 1], values[boneIndexOffset[0] + 2], values[boneIndexOffset[0] + 3]);
}

glm::vec4 VertexAccumulator::getBoneWeight() const
{
  if (boneWeightOffset.empty())
    return glm::vec4(1.0, 0.0, 0.0, 0.0);
  return glm::vec4(values[boneWeightOffset[0] + 0], values[boneWeightOffset[0] + 1], values[boneWeightOffset[0] + 2], values[boneWeightOffset[0] + 3]);
}

void Animation::Channel::calcBeginEndTimes()
{
  positionTimeBegin = tBeginTime(position);
  positionTimeEnd   = tEndTime(position);
  rotationTimeBegin = tBeginTime(rotation);
  rotationTimeEnd   = tEndTime(rotation);
  scaleTimeBegin    = tBeginTime(scale);
  scaleTimeEnd      = tEndTime(scale);
}

float Animation::Channel::beginTime() const
{
  return std::min(positionTimeBegin, std::min(rotationTimeBegin, scaleTimeBegin));
}

float Animation::Channel::endTime() const
{
  return std::max(positionTimeEnd, std::max(rotationTimeEnd, scaleTimeEnd));
}

glm::mat4 Animation::Channel::calculateTransform(float time, Channel::State before, Channel::State after) const
{
  glm::vec3 vScale       = scale.empty()    ? glm::vec3(1,1,1) : mix(scale.data(), scale.size(), calculateAnimationTime(time, scaleTimeBegin, scaleTimeEnd,  before, after));
  glm::quat qRotation    = rotation.empty() ? glm::quat()      : slerp(rotation.data(), rotation.size(), calculateAnimationTime(time, rotationTimeBegin, rotationTimeEnd, before, after) );
  glm::vec3 vTranslation = position.empty() ? glm::vec3(0,0,0) : mix(position.data(), position.size(), calculateAnimationTime(time, positionTimeBegin, positionTimeEnd, before, after));

  return glm::scale(glm::translate(mat4unity, vTranslation) * glm::mat4_cast(qRotation), vScale);
}

void Animation::calculateLocalTransforms(float time, glm::mat4* data, uint32_t size) const
{
  CHECK_LOG_THROW(size != channels.size(), "Wrong channel count");
  for (uint32_t i = 0; i < channels.size(); ++i, ++data)
    *data = channels[i].calculateTransform(time, channelBefore[i], channelAfter[i]);
}

void Geometry::pushVertex(const VertexAccumulator& vertexAccumulator)
{
  vertices.insert(end(vertices), cbegin(vertexAccumulator.values), cend(vertexAccumulator.values));
}

void Geometry::setVertex(uint32_t position, const VertexAccumulator& vertexAccumulator)
{
  std::copy(begin(vertexAccumulator.values), end(vertexAccumulator.values), begin(vertices) + position );
}

void Geometry::getVertex(uint32_t position, VertexAccumulator& vertexAccumulator)
{
  std::copy(begin(vertices) + position, begin(vertices) + position + vertexAccumulator.values.size(), begin(vertexAccumulator.values) );
}

glm::vec4 Material::getProperty(const std::string& name, glm::vec4 defaultValue) const
{
  auto it = properties.find(name);
  if (it != end(properties))
    return it->second;
  else
    return defaultValue;
}

void copyAndConvertVertices(std::vector<float>& targetBuffer, const std::vector<VertexSemantic>& targetSemantic, const std::vector<float>& sourceBuffer, const std::vector<VertexSemantic>& sourceSemantic)
{
  // check if semantics are the same ( fast path )
  if (targetSemantic == sourceSemantic)
  {
    std::copy(begin(sourceBuffer), end(sourceBuffer), std::back_inserter(targetBuffer));
    return;
  }
  // semantics are different - we need to do remapping
  std::vector<float>    defaultValues( calcVertexSize(targetSemantic) );
  std::vector<float>    targetValues( calcVertexSize(targetSemantic) );
  std::vector<uint32_t> sourceValuesIndex( calcVertexSize(targetSemantic) );

  // setup default values
  std::fill(begin(defaultValues), end(defaultValues), 0.0f);
  uint32_t offset=0;
  for (const auto& t : targetSemantic)
  {
    switch (t.type)
    {
    case VertexSemantic::Position:
      if (t.size == 4)
        defaultValues[offset + 3] = 1.0;
      break;
    case VertexSemantic::Color:
      for(uint32_t i=0;i<t.size; ++i)
        defaultValues[offset + i] = 1.0;
      break;
    case VertexSemantic::Normal:
      defaultValues[offset+t.size-1] = 1.0;
      break;
    case VertexSemantic::Tangent:
      defaultValues[offset + 0] = 1.0;
      break;
    case VertexSemantic::Bitangent:
      defaultValues[offset + 1] = 1.0;
      break;
    case VertexSemantic::BoneWeight:
      defaultValues[offset + 0] = 1.0;
      break;
    default:
      break;
    }
    offset += t.size;
  }

  // setup remapping
  offset = 0;

  std::fill(begin(sourceValuesIndex), end(sourceValuesIndex), UINT32_MAX);
  uint32_t currentTargetColor    = 0;
  uint32_t currentTargetTexCoord = 0;
  for (const auto& t : targetSemantic)
  {
    uint32_t i = 0;
    uint32_t sourceOffset = 0;
    uint32_t currentSourceColor = 0;
    uint32_t currentSourceTexCoord = 0;
    for (; i < sourceSemantic.size(); ++i)
    {
      if (sourceSemantic[i].type == t.type)
      {
        if (t.type == VertexSemantic::Color && currentSourceColor<currentTargetColor)
        {
          currentSourceColor++;
          sourceOffset += sourceSemantic[i].size;
          continue;
        }
        if (t.type == VertexSemantic::TexCoord && currentSourceTexCoord<currentTargetTexCoord)
        {
          currentSourceTexCoord++;
          sourceOffset += sourceSemantic[i].size;
          continue;
        }
        break;
      }
      sourceOffset += sourceSemantic[i].size;
    }
    if (i<sourceSemantic.size())
    {
      for (uint32_t j = 0; j<t.size && j<sourceSemantic[i].size; ++j)
      {
        sourceValuesIndex[offset+j] = sourceOffset+j;
      }
    }

    offset += t.size;
  }
  uint32_t sourceVertexSize = calcVertexSize(sourceSemantic);
  for (uint32_t i = 0; i < sourceBuffer.size(); i += sourceVertexSize)
  {
    targetValues = defaultValues;
    for (uint32_t j = 0; j<sourceValuesIndex.size(); ++j)
    {
      if (sourceValuesIndex[j] != UINT32_MAX)
        targetValues[j] = sourceBuffer[i + sourceValuesIndex[j]];
    }
    std::copy(begin(targetValues), end(targetValues), std::back_inserter(targetBuffer));
  }
}

void transformGeometry(const glm::mat4& matrix, Geometry& geometry)
{
  VertexAccumulator acc(geometry.semantic);
  uint32_t vertexCount = geometry.getVertexCount();
  uint32_t vertexSize  = calcVertexSize(geometry.semantic);

  glm::mat3 matrix3(matrix);
  glm::vec4 value;
  glm::vec3 value3;
  for (uint32_t i = 0; i < vertexCount; ++i)
  {
    geometry.getVertex(i*vertexSize, acc);
    uint32_t texCoordChannel = 0;
    uint32_t colorChannel = 0;
    for (auto& s : geometry.semantic)
    {
      switch (s.type)
      {
      case VertexSemantic::Position:
        value = acc.getPosition();
        value = matrix * value;
        acc.set(VertexSemantic::Position, value.x / value.w, value.y / value.w, value.z / value.w);
        break;
      case VertexSemantic::Normal:
        value  = acc.getNormal();
        value3 = glm::vec3(value.x, value.y, value.z);
        value3 = matrix3 * value3;
        acc.set(VertexSemantic::Normal, value3.x, value3.y, value3.z);
        break;
      case VertexSemantic::TexCoord:
      case VertexSemantic::Color:
      case VertexSemantic::BoneIndex:
      case VertexSemantic::BoneWeight:
        // texcoords, colors, bone indices and weights are not modified
        break;
      case VertexSemantic::Tangent:
        value  = acc.getTangent();
        value3 = glm::vec3(value.x, value.y, value.z);
        value3 = matrix3 * value3;
        acc.set(VertexSemantic::Tangent, value3.x, value3.y, value3.z);
        break;
      case VertexSemantic::Bitangent:
        value = acc.getBitangent();
        value3 = glm::vec3(value.x, value.y, value.z);
        value3 = matrix3 * value3;
        acc.set(VertexSemantic::Bitangent, value3.x, value3.y, value3.z);
        break;
      }
    }
    geometry.setVertex(i*vertexSize, acc);
  }
}

void mergeAsset(Asset& parentAsset, uint32_t parentBone, Asset& childAsset)
{
  uint32_t parentMaterialCount = parentAsset.materials.size();
  uint32_t parentGeometryCount = parentAsset.geometries.size();
  uint32_t parentBoneCount     = parentAsset.skeleton.bones.size();
  float    fParentBoneCount    = parentBoneCount;
  float    fParentBone         = parentBone;

  // copy bones
  for (uint32_t i = 0; i < childAsset.skeleton.boneNames.size(); ++i)
  {
    Skeleton::Bone bone  = childAsset.skeleton.bones[i];
    std::string boneName = childAsset.skeleton.boneNames[i];
    if (bone.parentIndex == UINT32_MAX)
      bone.parentIndex = parentBone;
    else
      bone.parentIndex += parentBoneCount;
    uint32_t currentBoneIndex = parentAsset.skeleton.bones.size();
    parentAsset.skeleton.bones.push_back(bone);
    parentAsset.skeleton.boneNames.push_back(boneName);
    parentAsset.skeleton.invBoneNames.insert({ boneName, currentBoneIndex });
  }

  // copy materials
  std::copy(begin(childAsset.materials), end(childAsset.materials), std::back_inserter(parentAsset.materials));

  // copy geometries. Update bone weights and material indices
  std::copy(begin(childAsset.geometries), end(childAsset.geometries), std::back_inserter(parentAsset.geometries));
  for (auto git = begin(parentAsset.geometries) + parentGeometryCount; git != end(parentAsset.geometries); ++git)
  {
    git->materialIndex = git->materialIndex + parentMaterialCount;

    // find vertex weight and vertex index
    uint32_t offset = 0, boneWeightOffset = UINT32_MAX, boneIndexOffset = UINT32_MAX, boneWeightSize = UINT32_MAX, boneIndexSize = UINT32_MAX;
    for (VertexSemantic s : git->semantic)
    {
      if (s.type == VertexSemantic::BoneWeight)
      {
        boneWeightOffset = offset;
        boneWeightSize   = s.size;
      }
      if (s.type == VertexSemantic::BoneIndex)
      {
        boneIndexOffset = offset;
        boneIndexSize   = s.size;
      }
      offset += s.size;
    }
    if (boneWeightOffset == UINT32_MAX || boneIndexOffset == UINT32_MAX)
    {
      LOG_WARNING << "Geometry semantic has no bone weight or no bone index" << std::endl;
      continue;
    }
    uint32_t vertexSize = calcVertexSize(git->semantic);
    for (size_t i = 0; i < git->vertices.size(); i += vertexSize)
    {
      for (uint32_t j = i + boneWeightOffset, k = i + boneIndexOffset; j < i + boneWeightOffset + boneWeightSize; ++j, ++k)
        if (git->vertices[j]!=0.0f) // skip if boneWeight==0.0
          git->vertices[k] += fParentBoneCount;
    }
  }
  // FIXME - as for now we dont copy the animations
  parentAsset.skeleton.refreshChildren();
}

std::vector<glm::mat4> calculateResetPosition(const Asset& asset)
{
  std::vector<glm::mat4> globalTransforms(std::max<size_t>(1, asset.skeleton.bones.size()));
  globalTransforms[0] = asset.skeleton.invGlobalTransform * asset.skeleton.bones[0].localTransformation;
  for (uint32_t boneIndex = 1; boneIndex < asset.skeleton.bones.size(); ++boneIndex)
    globalTransforms[boneIndex] = globalTransforms[asset.skeleton.bones[boneIndex].parentIndex] * asset.skeleton.bones[boneIndex].localTransformation;
  
  std::vector<glm::mat4> resetTransforms(asset.skeleton.bones.size());
  for (uint32_t boneIndex = 0; boneIndex < asset.skeleton.bones.size(); ++boneIndex)
    resetTransforms[boneIndex] = globalTransforms[boneIndex] * asset.skeleton.bones[boneIndex].offsetMatrix;
  return resetTransforms;
}

BoundingBox calculateBoundingBox(const Asset& asset, uint32_t renderMask)
{
  std::vector<glm::mat4> resetTransforms = calculateResetPosition(asset);

  BoundingBox bbox;
  for (uint32_t geomIndex = 0; geomIndex < asset.geometries.size(); ++geomIndex)
    if (asset.geometries[geomIndex].renderMask == renderMask)
      bbox += calculateBoundingBox(asset.geometries[geomIndex], resetTransforms);
  return bbox;
}

BoundingBox calculateBoundingBox(const Geometry& geometry, const std::vector<glm::mat4>& bones)
{
  uint32_t vertexStride = calcVertexSize(geometry.semantic);
  uint32_t positionOffset = UINT32_MAX;
  uint32_t indexOffset    = UINT32_MAX;
  uint32_t weightOffset   = UINT32_MAX;
  uint32_t indexSize = 0;
  uint32_t offset = 0;
  for (const auto& a : geometry.semantic)
  {
    if (a.type == VertexSemantic::Position)
      positionOffset = offset;
    if (a.type == VertexSemantic::BoneIndex)
    {
      indexOffset    = offset;
      indexSize      = a.size;
    }
    if (a.type == VertexSemantic::BoneWeight)
      weightOffset   = offset;
    offset += a.size;
  }

  BoundingBox bbox;
  if (positionOffset == UINT32_MAX || indexOffset == UINT32_MAX || weightOffset == UINT32_MAX)
    return bbox;

  for (uint32_t i = 0; i < geometry.vertices.size(); i += vertexStride)
  {
    glm::mat4 boneTransform = bones[int(geometry.vertices[i + indexOffset + 0])] * geometry.vertices[i + weightOffset + 0];
    for (uint32_t j = 1; j<indexSize; ++j)
      boneTransform += bones[int(geometry.vertices[i + indexOffset + j])] * geometry.vertices[i + weightOffset + j];
    glm::vec4 pos(geometry.vertices[i + positionOffset + 0], geometry.vertices[i + positionOffset + 1], geometry.vertices[i + positionOffset + 2], 1.0f);
    pos = boneTransform * pos;

    bbox += glm::vec3(pos.x / pos.w, pos.y / pos.w, pos.z / pos.w);
  }
  return bbox;
}

BoundingBox calculateBoundingBox(const Skeleton& skeleton, const Animation& animation, bool addFictionalLeaves)
{
  // collect all timepoints
  std::set<float> timePoints;
  for ( const auto& c : animation.channels)
  {
    for (const auto& p : c.position)
      timePoints.insert(p.time);
    for (const auto& p : c.rotation)
      timePoints.insert(p.time);
    for (const auto& p : c.scale)
      timePoints.insert(p.time);
  }
  std::vector<glm::mat4> localTransforms(animation.channels.size());

  // calculate bones position for each time point. Add it to bbox
  BoundingBox bbox;
  for (const auto& time : timePoints)
  {
    animation.calculateLocalTransforms(time, localTransforms.data(), localTransforms.size());

    std::queue<std::tuple<uint32_t, glm::mat4>> boneQueue;
    boneQueue.push(std::make_tuple(0, glm::mat4()));
    while (!boneQueue.empty())
    {
      auto boneData = boneQueue.front();
      boneQueue.pop();
      uint32_t boneIndex = std::get<0>(boneData);
      if (skeleton.bones[boneIndex].boneTag != 1)
        continue;
      glm::mat4 globalParentTransform = std::get<1>(boneData);
      glm::mat4 localCurrentTransform = skeleton.bones[boneIndex].localTransformation;
      auto it = animation.invChannelNames.find(skeleton.boneNames[boneIndex]);
      if (it != end(animation.invChannelNames))
        localCurrentTransform = localTransforms[it->second];
      glm::mat4 globalCurrentTransform = globalParentTransform * localCurrentTransform;
      glm::mat4 targetMatrix = skeleton.invGlobalTransform * globalCurrentTransform;
      glm::vec4 pt = targetMatrix * glm::vec4(0,0,0,1);
      bbox += glm::vec3( pt.x/pt.w, pt.y/pt.w, pt.z/pt.w);

      uint32_t firstIndex = skeleton.bones[boneIndex].childrenOffset;
      uint32_t childrenSize = skeleton.bones[boneIndex].childrenSize;
      // FIXME : there's no way to calculate the length of leaf bones. Let's just use last localCurrentTransform again...
      // some assimp loaders (BVH for example) add fictional bones at the leafs
      if (addFictionalLeaves && childrenSize == 0)
      {
        pt = targetMatrix * localCurrentTransform * glm::vec4(0, 0, 0, 1);
        bbox += glm::vec3(pt.x / pt.w, pt.y / pt.w, pt.z / pt.w);
      }

      for (uint32_t i = 0, index = firstIndex; i<childrenSize; ++i, ++index)
        boneQueue.push(std::make_tuple(skeleton.children[index], globalCurrentTransform));
    }
  }
  return bbox;
}

}
