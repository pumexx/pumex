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

#include <pumex/AssetLoaderAssimp.h>
#include <pumex/Viewer.h>
#include <pumex/utils/Log.h>
#include <queue>
#include <map>
#include <tuple>
using namespace pumex;

AssetLoaderAssimp::AssetLoaderAssimp()
{
}

glm::mat4 toMat4( const aiMatrix4x4& matrix )
{
  return glm::mat4
    (
    matrix.a1, matrix.b1, matrix.c1, matrix.d1,
    matrix.a2, matrix.b2, matrix.c2, matrix.d2,
    matrix.a3, matrix.b3, matrix.c3, matrix.d3,
    matrix.a4, matrix.b4, matrix.c4, matrix.d4
    );
}

Animation::Channel::State toChannelState(aiAnimBehaviour behaviour)
{
  switch (behaviour)
  {
  case aiAnimBehaviour_DEFAULT:  return Animation::Channel::REPEAT;
  case aiAnimBehaviour_CONSTANT: return Animation::Channel::CLAMP;
  case aiAnimBehaviour_LINEAR:   return Animation::Channel::CLAMP;
  case aiAnimBehaviour_REPEAT:   return Animation::Channel::REPEAT;
  default :                      return Animation::Channel::CLAMP;
  }
  return Animation::Channel::CLAMP;
}

void getMaterialPropertyColor(Material& mat, aiMaterial* aiMat, const char* key, unsigned int type, unsigned int index)
{
  aiColor3D value{0.f, 0.f, 0.f};
  if (AI_SUCCESS == aiMat->Get(key, 0, 0, value))
    mat.properties.insert({ std::string(key), glm::vec4(value.r, value.g, value.b, 1.0f) });
}

void getMaterialPropertyFloat(Material& mat, aiMaterial* aiMat, const char* key, unsigned int type, unsigned int index)
{
  float value{0.0f};
  if (AI_SUCCESS == aiMat->Get(key, 0, 0, value))
    mat.properties.insert({ std::string(key), glm::vec4(value, 0.0f, 0.0f, 0.0f) });
}

void getMaterialPropertyInteger(Material& mat, aiMaterial* aiMat, const char* key, unsigned int type, unsigned int index)
{
  int value{0};
  if (AI_SUCCESS == aiMat->Get(key, 0, 0, value))
    mat.properties.insert({ std::string(key), glm::vec4(value, 0.0f, 0.0f, 0.0f) });
}

std::shared_ptr<Asset> AssetLoaderAssimp::load(std::shared_ptr<Viewer> viewer, const std::string& fileName, bool animationOnly, const std::vector<VertexSemantic>& requiredSemantic)
{
  auto fullFileName = viewer->getAbsoluteFilePath(fileName);
  CHECK_LOG_THROW(fullFileName.empty(), "Cannot find model file " << fileName);
  const aiScene* scene = Importer.ReadFile(fullFileName.c_str(), importFlags);
  CHECK_LOG_THROW(scene == nullptr, "Cannot load model file : " << fullFileName)

  //creating asset
  std::shared_ptr<Asset> asset = std::make_shared<Asset>();
  asset->fileName              = fileName;
  asset->skeleton.invGlobalTransform = glm::inverse( toMat4( scene->mRootNode->mTransformation ) );
  if (!animationOnly)
  {
    // STEP 1 :  collect ALL NODES and write it into BONE hierarchy
    {
      std::queue<std::tuple<const aiNode*, glm::mat4, uint32_t>> nodeQueue;
      nodeQueue.push(std::make_tuple(scene->mRootNode, glm::mat4(), std::numeric_limits<uint32_t>::max()));
      while (!nodeQueue.empty())
      {
        auto nodeData = nodeQueue.front();
        nodeQueue.pop();
        const aiNode* node                = std::get<0>(nodeData);
        glm::mat4 globalParentTransform  = std::get<1>(nodeData);
        glm::mat4 localCurrentTransform  = toMat4(node->mTransformation);
        glm::mat4 globalCurrentTransform = globalParentTransform * localCurrentTransform;

        uint32_t boneIndex = asset->skeleton.bones.size();
        Skeleton::Bone bone;
        bone.parentIndex         = std::get<2>(nodeData);
        bone.localTransformation = localCurrentTransform;
        asset->skeleton.bones.emplace_back(bone);
        asset->skeleton.boneNames.push_back(node->mName.C_Str());
        asset->skeleton.invBoneNames.insert({ node->mName.C_Str(), boneIndex });

        for (uint32_t i = 0; i<node->mNumChildren; ++i)
          nodeQueue.push(std::make_tuple(node->mChildren[i], globalCurrentTransform, boneIndex));
      }
    }
    asset->skeleton.refreshChildren();

    // STEP 2 : find bone offset martices and mark bones
    {
      std::queue<std::tuple<const aiNode*, glm::mat4>> nodeQueue;
      nodeQueue.push(std::make_tuple(scene->mRootNode, glm::mat4()));
      while (!nodeQueue.empty())
      {
        auto nodeData = nodeQueue.front();
        nodeQueue.pop();
        const aiNode* node               = std::get<0>(nodeData);
        glm::mat4 globalParentTransform  = std::get<1>(nodeData);
        glm::mat4 localCurrentTransform  = toMat4(node->mTransformation);
        glm::mat4 globalCurrentTransform = globalParentTransform * localCurrentTransform;

        for (uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
          aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

          for (uint32_t j = 0; j < mesh->mNumBones; ++j)
          {
            aiBone* bone = mesh->mBones[j];
            auto it = asset->skeleton.invBoneNames.find(bone->mName.C_Str());
            if (it != end(asset->skeleton.invBoneNames))
            {
              uint32_t boneIndex = it->second;
              asset->skeleton.bones[boneIndex].offsetMatrix = toMat4(bone->mOffsetMatrix);

              while (boneIndex != std::numeric_limits<uint32_t>::max())
              {
                asset->skeleton.bones[boneIndex].boneTag = 1;
                boneIndex = asset->skeleton.bones[boneIndex].parentIndex;
              }
            }
          }
        }
        for (uint32_t i = 0; i<node->mNumChildren; ++i)
          nodeQueue.push(std::make_tuple(node->mChildren[i], globalCurrentTransform));
      }
    }

    // STEP 3 : create bone weights for each mesh
    std::map<aiMesh*, std::vector<glm::vec4>> vertexWeights;
    std::map<aiMesh*, std::vector<glm::vec4>> vertexIndices;
    for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
    {
      aiMesh* mesh = scene->mMeshes[i];
      // if mesh has bones attached then calculate bone weights and indices
      if (mesh->mNumBones > 0)
      {
        std::vector<std::vector<std::tuple<float,uint32_t>>> weights(mesh->mNumVertices);
        for (uint32_t j = 0; j < mesh->mNumBones; ++j)
        {
          aiBone* bone = mesh->mBones[j];
          auto it = asset->skeleton.invBoneNames.find(bone->mName.C_Str());
          if (it == end(asset->skeleton.invBoneNames))
            continue;
          uint32_t boneIndex = it->second;
          for ( uint32_t k=0; k<bone->mNumWeights; ++k )
            weights[bone->mWeights[k].mVertexId].push_back(std::make_tuple(bone->mWeights[k].mWeight, boneIndex));
        }
        std::vector<glm::vec4> vWeights(weights.size());
        std::vector<glm::vec4> vIndices(weights.size());
        for (uint32_t j = 0; j < weights.size(); ++j)
        {
          std::sort(begin(weights[j]), end(weights[j]), [](const std::tuple<float, uint32_t>& lhs, const std::tuple<float, uint32_t>& rhs){ return std::get<0>(lhs)>std::get<0>(rhs); });
          // no more than 4 weights will be used
          for (uint32_t k = 0; k<weights[j].size() && k<4; ++k)
            std::tie(vWeights[j][k], vIndices[j][k]) = weights[j][k];
        }
        vertexWeights.insert({ mesh, vWeights });
        vertexIndices.insert({ mesh, vIndices });
      }
    }

    // STEP 4 : collect ALL MESHES according to NODE hierarchy, add previously created bone weights
    // If mesh does not have bone weights and indices - attach it to current bone with 100% weight
    // Note : direct acyclic graph is converted to ordinary tree
    {
      std::queue<std::tuple<const aiNode*, glm::mat4>> nodeQueue;
      nodeQueue.push(std::make_tuple(scene->mRootNode, glm::mat4()));
      while (!nodeQueue.empty())
      {
        auto nodeData = nodeQueue.front();
        nodeQueue.pop();
        const aiNode* node               = std::get<0>(nodeData);
        glm::mat4 globalParentTransform  = std::get<1>(nodeData);
        glm::mat4 localCurrentTransform  = toMat4(node->mTransformation);
        glm::mat4 globalCurrentTransform = globalParentTransform * localCurrentTransform;
        glm::mat3 globalCurrentRotation(globalCurrentTransform);

        // it will always have a value because all nodes have been added to invBoneNames in step 1
        auto it = asset->skeleton.invBoneNames.find(node->mName.C_Str());
        uint32_t boneIndex = it->second;

        for (uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
          aiMesh*  mesh           = scene->mMeshes[node->mMeshes[i]];
          Geometry geometry;
          geometry.name          = mesh->mName.C_Str();
          geometry.materialIndex = mesh->mMaterialIndex;
          switch (mesh->mPrimitiveTypes)
          {
          case aiPrimitiveType_POINT:    geometry.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;    break;
          case aiPrimitiveType_LINE:     geometry.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;     break;
          case aiPrimitiveType_TRIANGLE: geometry.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
          default:                       geometry.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
          }

          // calculate proper vertex semantic
          std::vector<VertexSemantic> thisSemantic;
          if (requiredSemantic.empty())
          {
            thisSemantic.push_back(VertexSemantic(VertexSemantic::Position, 3));
            if (mesh->HasNormals())
              thisSemantic.push_back(VertexSemantic(VertexSemantic::Normal, 3));
            for (uint32_t k = 0; k < mesh->GetNumColorChannels(); ++k)
              thisSemantic.push_back(VertexSemantic(VertexSemantic::Color, 4));
            for (uint32_t k = 0; k < mesh->GetNumUVChannels(); ++k)
            {
              switch (mesh->mNumUVComponents[k])
              {
              case 2:
                thisSemantic.push_back(VertexSemantic(VertexSemantic::TexCoord, 2));
                break;
              case 3:
                thisSemantic.push_back(VertexSemantic(VertexSemantic::TexCoord, 3));
                break;
              }
            }
            if (mesh->HasTangentsAndBitangents())
            {
              thisSemantic.push_back(VertexSemantic(VertexSemantic::Tangent, 3));
              thisSemantic.push_back(VertexSemantic(VertexSemantic::Bitangent, 3));
            }
            // all our models use bone weight and bone indices
            thisSemantic.push_back(VertexSemantic(VertexSemantic::BoneWeight, 4));
            thisSemantic.push_back(VertexSemantic(VertexSemantic::BoneIndex, 4));
          }
          else
          {
            thisSemantic = requiredSemantic;
            bool requiredHasBoneIndex  = false;
            bool requiredHasBoneWeight = false;
            for (const auto& s : requiredSemantic)
            {
              if (s.type == VertexSemantic::BoneIndex)
                requiredHasBoneIndex = true;
              if (s.type == VertexSemantic::BoneWeight)
                requiredHasBoneWeight = true;
            }
            if (!requiredHasBoneIndex)
              thisSemantic.push_back(VertexSemantic(VertexSemantic::BoneIndex, 1));
            if (!requiredHasBoneWeight)
              thisSemantic.push_back(VertexSemantic(VertexSemantic::BoneWeight, 1));
          }
          geometry.semantic = thisSemantic;

          // copying vertex and index data
          uint32_t vertexCount = mesh->mNumVertices * calcVertexSize(thisSemantic);
          uint32_t indexCount  = mesh->mNumFaces * calcPrimitiveSize(geometry.topology);

          geometry.vertices.reserve(vertexCount);
          geometry.indices.reserve(indexCount);
          auto weightIt = vertexWeights.find(mesh);
          auto indexIt  = vertexIndices.find(mesh);
          VertexAccumulator acc(thisSemantic);

          for (uint32_t j = 0; j < mesh->mNumVertices; ++j)
          {
            uint32_t currentColor    = 0;
            uint32_t currentTexCoord = 0;
            for (auto semantic : geometry.semantic)
            {
              switch (semantic.type)
              {
              case VertexSemantic::Position: // always 3
                acc.set(VertexSemantic::Position, mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z);
                break;
              case VertexSemantic::Normal: // always 3
                if (mesh->HasNormals())
                  acc.set(VertexSemantic::Normal, mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z);
                break;
              case VertexSemantic::TexCoord:
                if (currentTexCoord < mesh->GetNumUVChannels())
                {
                  aiVector3D& vec = mesh->mTextureCoords[currentTexCoord][j];
                  acc.set(VertexSemantic::TexCoord, currentTexCoord, vec.x, vec.y, vec.z);
                  currentTexCoord++;
                }
                break;
              case VertexSemantic::Color:
                if (currentColor < mesh->GetNumColorChannels())
                {
                  aiColor4D& color = mesh->mColors[currentColor][j];
                  acc.set(VertexSemantic::Color, currentColor, color.r, color.g, color.b, color.a);
                  currentColor++;
                }
                break;
              case VertexSemantic::Tangent: // always 3
                if (mesh->HasTangentsAndBitangents())
                {
                  aiVector3D& vec = mesh->mTangents[j];
                  acc.set(VertexSemantic::Tangent, vec.x, vec.y, vec.z);
                }
                break;
              case VertexSemantic::Bitangent: // always 3
                if (mesh->HasTangentsAndBitangents())
                {
                  aiVector3D& vec = mesh->mBitangents[j];
                  acc.set(VertexSemantic::Bitangent, vec.x, vec.y, vec.z);
                }
                break;
              case VertexSemantic::BoneWeight: // always 4
                if (weightIt != end(vertexWeights))
                  acc.set(VertexSemantic::BoneWeight, weightIt->second[j].x, weightIt->second[j].y, weightIt->second[j].z, weightIt->second[j].w);
                else
                  acc.set(VertexSemantic::BoneWeight, 1.0f);
                break;
              case VertexSemantic::BoneIndex: // always 4
                if (indexIt != end(vertexIndices))
                  acc.set(VertexSemantic::BoneIndex, indexIt->second[j].x, indexIt->second[j].y, indexIt->second[j].z, indexIt->second[j].w);
                else
                  acc.set(VertexSemantic::BoneIndex, boneIndex);
                break;
              }
            }
            geometry.pushVertex(acc);
          }

          // copying indices
          for (uint32_t j = 0; j < mesh->mNumFaces; ++j)
          {
            for (uint32_t k = 0; k < mesh->mFaces[j].mNumIndices; ++k)
              geometry.indices.push_back( mesh->mFaces[j].mIndices[k]);
          }
          asset->geometries.emplace_back(geometry);
        }
        for (uint32_t i = 0; i<node->mNumChildren; ++i)
          nodeQueue.push(std::make_tuple(node->mChildren[i], globalCurrentTransform));
      }
    }

    // STEP 5 : load material description
    std::vector<aiTextureType> texTypes = { aiTextureType_DIFFUSE, aiTextureType_SPECULAR, aiTextureType_AMBIENT, aiTextureType_EMISSIVE, aiTextureType_HEIGHT, aiTextureType_NORMALS, aiTextureType_SHININESS, aiTextureType_OPACITY, aiTextureType_DISPLACEMENT, aiTextureType_LIGHTMAP, aiTextureType_REFLECTION };
    for (uint32_t i = 0; i < scene->mNumMaterials; ++i)
    {
      Material material;
      aiString matName;
      scene->mMaterials[i]->Get(AI_MATKEY_NAME,matName);
      material.name = matName.C_Str();
      for (uint32_t j = 0; j < texTypes.size(); ++j)
      {
        for (uint32_t k = 0; k<scene->mMaterials[i]->GetTextureCount(texTypes[j]); ++k)
        {
          aiString texName;
          aiReturn res = scene->mMaterials[i]->GetTexture(texTypes[j], k, &texName);
          material.textures.insert({ j, texName.C_Str() });
        }
      }
      // There is no civilized way to iterate over assimp material properties...
      getMaterialPropertyColor(material, scene->mMaterials[i], AI_MATKEY_COLOR_DIFFUSE);
      getMaterialPropertyColor(material, scene->mMaterials[i], AI_MATKEY_COLOR_AMBIENT);
      getMaterialPropertyColor(material, scene->mMaterials[i], AI_MATKEY_COLOR_SPECULAR);
      getMaterialPropertyColor(material, scene->mMaterials[i], AI_MATKEY_COLOR_EMISSIVE);
      getMaterialPropertyColor(material, scene->mMaterials[i], AI_MATKEY_COLOR_TRANSPARENT);
      getMaterialPropertyColor(material, scene->mMaterials[i], AI_MATKEY_COLOR_REFLECTIVE);
      getMaterialPropertyFloat(material, scene->mMaterials[i], AI_MATKEY_OPACITY);
      getMaterialPropertyFloat(material, scene->mMaterials[i], AI_MATKEY_BUMPSCALING);
      getMaterialPropertyFloat(material, scene->mMaterials[i], AI_MATKEY_SHININESS);
      getMaterialPropertyFloat(material, scene->mMaterials[i], AI_MATKEY_SHININESS_STRENGTH);
      getMaterialPropertyFloat(material, scene->mMaterials[i], AI_MATKEY_REFLECTIVITY);
      getMaterialPropertyFloat(material, scene->mMaterials[i], AI_MATKEY_REFRACTI);
      asset->materials.push_back(material);
    }
  }

  // STEP 6 : load animations
  for (uint32_t i = 0; i < scene->mNumAnimations; ++i)
  {
    aiAnimation* anim = scene->mAnimations[i];
    Animation animation;
    animation.name            = anim->mName.C_Str();
    float ticksPerSecond  = anim->mTicksPerSecond;
    for (uint32_t j = 0; j < anim->mNumChannels; ++j)
    {
      aiNodeAnim* nodeAnim = anim->mChannels[j];
      Animation::Channel channel;
      std::string channelName = nodeAnim->mNodeName.C_Str();

      //positions
      for (uint32_t k = 0; k < nodeAnim->mNumPositionKeys; ++k)
      {
        const aiVectorKey& key = nodeAnim->mPositionKeys[k];
        channel.position.push_back(TimeLine<glm::vec3>(key.mTime / ticksPerSecond, glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z)));
      }
      std::sort(begin(channel.position), end(channel.position));

      // rotations
      for (uint32_t k = 0; k < nodeAnim->mNumRotationKeys; ++k)
      {
        const aiQuatKey& key = nodeAnim->mRotationKeys[k];
        channel.rotation.push_back(TimeLine<glm::quat>(key.mTime / ticksPerSecond, glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z)));
      }
      std::sort(begin(channel.rotation), end(channel.rotation));

      // scales
      for (uint32_t k = 0; k < nodeAnim->mNumScalingKeys; ++k)
      {
        const aiVectorKey& key = nodeAnim->mScalingKeys[k];
        channel.scale.push_back(TimeLine<glm::vec3>(key.mTime / ticksPerSecond, glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z)));
      }
      std::sort(begin(channel.scale), end(channel.scale));
      channel.calcBeginEndTimes();

      uint32_t animationIndex = animation.channels.size();
      animation.channels.emplace_back(channel);
      animation.channelNames.push_back(channelName);
      animation.channelBefore.push_back(toChannelState(nodeAnim->mPreState));
      animation.channelAfter.push_back(toChannelState(nodeAnim->mPostState));
      animation.invChannelNames.insert({channelName,animationIndex});
    }
    asset->animations.emplace_back(animation);
  }

  return asset;
}

// ugh, one of the ugliest pieces of code I wrote in recent years. Thank you assimp...
