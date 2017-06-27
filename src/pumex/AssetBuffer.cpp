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

#include <pumex/AssetBuffer.h>
#include <cstring>
#include <set>
#include <iterator>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

namespace pumex
{

AssetBuffer::AssetBuffer(const std::vector<AssetBufferVertexSemantics>& vertexSemantics, std::weak_ptr<DeviceMemoryAllocator> bufferAllocator, std::weak_ptr<DeviceMemoryAllocator> vertexIndexAllocator)
{
  for (const auto& vs : vertexSemantics)
  {
    semantics[vs.renderMask]         = vs.vertexSemantic;
    perRenderMaskData[vs.renderMask] = PerRenderMaskData(bufferAllocator, vertexIndexAllocator);
  }

  typeNames.push_back("<null>");
  invTypeNames.insert({ "<null>", 0 });
  typeDefinitions.push_back(AssetTypeDefinition());
  lodDefinitions.push_back(std::vector<AssetLodDefinition>());
}

AssetBuffer::~AssetBuffer()
{
}

uint32_t AssetBuffer::registerType(const std::string& typeName, const AssetTypeDefinition& tdef)
{
  if (invTypeNames.find(typeName) != invTypeNames.end())
    return 0;
  uint32_t typeID = typeNames.size();
  typeNames.push_back(typeName);
  invTypeNames.insert({ typeName, typeID });
  typeDefinitions.push_back(tdef);
  lodDefinitions.push_back(std::vector<AssetLodDefinition>());
  setDirty();
  return typeID;
}

uint32_t AssetBuffer::registerObjectLOD(uint32_t typeID, std::shared_ptr<Asset> asset, const AssetLodDefinition& ldef)
{
  if (typeID == 0 || typeNames.size() < typeID)
    return UINT32_MAX;
  uint32_t lodID = lodDefinitions[typeID].size();
  lodDefinitions[typeID].push_back(ldef);

  // check if this asset has been registered already
  uint32_t assetIndex = assets.size();
  for (uint32_t i = 0; i < assets.size(); ++i)
  {
    if (asset.get() == assets[i].get())
    {
      assetIndex = i;
      break;
    }
  }
  if (assetIndex == assets.size())
    assets.push_back(asset);
  assetMapping.insert({ AssetKey(typeID,lodID), asset });

  for (uint32_t i = 0; i<assets[assetIndex]->geometries.size(); ++i)
    geometryDefinitions.push_back(InternalGeometryDefinition(typeID, lodID, assets[assetIndex]->geometries[i].renderMask, assetIndex, i));
  setDirty();
  return lodID;
}

uint32_t AssetBuffer::getTypeID(const std::string& typeName) const
{
  auto it = invTypeNames.find(typeName);
  if (it == invTypeNames.end())
    return 0;
  return it->second;
}

std::string AssetBuffer::getTypeName(uint32_t typeID) const
{
  if (typeID >= typeNames.size())
    return typeNames[0];
  return typeNames[typeID];
}

uint32_t AssetBuffer::getLodID(uint32_t typeID, float distance) const
{
  if (typeID == 0 || typeNames.size()<typeID)
    return UINT32_MAX;
  for (uint32_t i = 0; i < lodDefinitions[typeID].size(); ++i)
    if (lodDefinitions[typeID][i].active(distance))
      return i;
  return UINT32_MAX;
}

std::shared_ptr<Asset> AssetBuffer::getAsset(uint32_t typeID, uint32_t lodID)
{
  auto it = assetMapping.find(AssetKey(typeID, lodID));
  if (it != assetMapping.end())
    return it->second;
  return std::shared_ptr<Asset>();
}

void AssetBuffer::validate(Device* device, CommandPool* commandPool, VkQueue queue)
{
  if (dirty)
  {
    // divide geometries according to renderMasks
    std::map<uint32_t, std::vector<InternalGeometryDefinition>> geometryDefinitionsByRenderMask;
    for (const auto& gd : geometryDefinitions)
    {
      auto it = geometryDefinitionsByRenderMask.find(gd.renderMask);
      if (it == geometryDefinitionsByRenderMask.end())
        it = geometryDefinitionsByRenderMask.insert({ gd.renderMask, std::vector<InternalGeometryDefinition>() }).first;
      it->second.push_back(gd);
    }

    for (auto& gd : geometryDefinitionsByRenderMask)
    {
      // only create asset buffers for render masks that have nonempty vertex semantic defined
      auto pdmit = perRenderMaskData.find(gd.first);
      if (pdmit == perRenderMaskData.end())
        continue;
      PerRenderMaskData& rmData = pdmit->second;

      std::vector<VertexSemantic> requiredSemantic;
      auto sit = semantics.find(gd.first);
      if (sit != semantics.end())
        requiredSemantic = sit->second;
      if (requiredSemantic.empty())
        continue;

      // Sort geometries according to typeID and lodID
      std::sort(gd.second.begin(), gd.second.end(), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs) { if (lhs.typeID != rhs.typeID) return lhs.typeID < rhs.typeID; return lhs.lodID < rhs.lodID; });

      VkDeviceSize                verticesSoFar   = 0;
      VkDeviceSize                indicesSoFar    = 0;
      rmData.vertices->resize(0);
      rmData.indices->resize(0);

      std::vector<AssetTypeDefinition>     assetTypes = typeDefinitions;
      std::vector<AssetLodDefinition>      assetLods;
      std::vector<AssetGeometryDefinition> assetGeometries;
      for (uint32_t t = 0; t < assetTypes.size(); ++t)
      {
        auto typePair = std::equal_range(gd.second.begin(), gd.second.end(), InternalGeometryDefinition(t, 0, 0, 0, 0), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs) {return lhs.typeID < rhs.typeID; });
        assetTypes[t].lodFirst = assetLods.size();
        for (uint32_t l = 0; l < lodDefinitions[t].size(); ++l)
        {
          auto lodPair = std::equal_range(typePair.first, typePair.second, InternalGeometryDefinition(t, l, 0, 0, 0), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs) {return lhs.lodID < rhs.lodID; });
          if (lodPair.first != lodPair.second)
          {
            AssetLodDefinition lodDef = lodDefinitions[t][l];
            lodDef.geomFirst = assetGeometries.size();
            for (auto it = lodPair.first; it != lodPair.second; ++it)
            {
              uint32_t indexCount = assets[it->assetIndex]->geometries[it->geometryIndex].getIndexCount();
              uint32_t firstIndex = indicesSoFar;
              uint32_t vertexOffset = verticesSoFar;
              assetGeometries.push_back(AssetGeometryDefinition(indexCount, firstIndex, vertexOffset));

              // calculating buffer sizes etc
              verticesSoFar += assets[it->assetIndex]->geometries[it->geometryIndex].getVertexCount();
              indicesSoFar += indexCount;

              // copying vertices to a vertex buffer
              copyAndConvertVertices(*(rmData.vertices), requiredSemantic, assets[it->assetIndex]->geometries[it->geometryIndex].vertices, assets[it->assetIndex]->geometries[it->geometryIndex].semantic);
              // copying indices to an index buffer
              const auto& indices = assets[it->assetIndex]->geometries[it->geometryIndex].indices;
              std::copy(indices.begin(), indices.end(), std::back_inserter(*(rmData.indices)));
            }
            lodDef.geomSize = assetGeometries.size() - lodDef.geomFirst;
            assetLods.push_back(lodDef);
          }
        }
        assetTypes[t].lodSize = assetLods.size() - assetTypes[t].lodFirst;
      }
      rmData.vertexBuffer->setDirty();
      rmData.indexBuffer->setDirty();
      rmData.typeBuffer->set(assetTypes);
      rmData.lodBuffer->set(assetLods);
      rmData.geomBuffer->set(assetGeometries);
    }

    dirty = false;
  }
  for (auto& prm : perRenderMaskData)
  {
    prm.second.vertexBuffer->validate(device, commandPool, queue);
    prm.second.indexBuffer->validate(device, commandPool, queue);
    prm.second.typeBuffer->validate(device, commandPool, queue);
    prm.second.lodBuffer->validate(device, commandPool, queue);
    prm.second.geomBuffer->validate(device, commandPool, queue);
  }
}

void AssetBuffer::cmdBindVertexIndexBuffer(Device* device, std::shared_ptr<CommandBuffer> commandBuffer, uint32_t renderMask, uint32_t vertexBinding) const
{
  auto prmit = perRenderMaskData.find(renderMask);
  if (prmit == perRenderMaskData.end())
  {
    LOG_WARNING << "AssetBuffer::bindVertexIndexBuffer() does not have this render mask defined" << std::endl;
    return;
  }
  VkBuffer vBuffer = prmit->second.vertexBuffer->getBufferHandle(device);
  VkBuffer iBuffer = prmit->second.indexBuffer->getBufferHandle(device);
  VkDeviceSize offsets = 0;
  vkCmdBindVertexBuffers(commandBuffer->getHandle(), vertexBinding, 1, &vBuffer, &offsets);
  vkCmdBindIndexBuffer(commandBuffer->getHandle(), iBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void AssetBuffer::cmdDrawObject(Device* device, std::shared_ptr<CommandBuffer> commandBuffer, uint32_t renderMask, uint32_t typeID, uint32_t firstInstance, float distanceToViewer) const
{
  auto prmit = perRenderMaskData.find(renderMask);
  if (prmit == perRenderMaskData.end())
  {
    LOG_WARNING << "AssetBuffer::drawObject() does not have this render mask defined" << std::endl;
    return;
  }
  std::vector<AssetTypeDefinition>     assetTypes      = prmit->second.typeBuffer->get();
  std::vector<AssetLodDefinition>      assetLods       = prmit->second.lodBuffer->get();
  std::vector<AssetGeometryDefinition> assetGeometries = prmit->second.geomBuffer->get();

  uint32_t lodFirst = assetTypes[typeID].lodFirst;
  uint32_t lodSize  = assetTypes[typeID].lodSize;
  for (unsigned int l = lodFirst; l < lodFirst + lodSize; ++l)
  {
    if (assetLods[l].active(distanceToViewer))
    {
      uint32_t geomFirst = assetLods[l].geomFirst;
      uint32_t geomSize  = assetLods[l].geomSize;
      for (uint32_t g = geomFirst; g < geomFirst + geomSize; ++g)
      {
        uint32_t indexCount   = assetGeometries[g].indexCount;
        uint32_t firstIndex   = assetGeometries[g].firstIndex;
        uint32_t vertexOffset = assetGeometries[g].vertexOffset;
        commandBuffer->cmdDrawIndexed(indexCount, 1, firstIndex, vertexOffset, firstInstance);
      }
    }
  }
}

std::shared_ptr<StorageBuffer<AssetTypeDefinition>> AssetBuffer::getTypeBuffer(uint32_t renderMask)
{
  auto it = perRenderMaskData.find(renderMask);
  CHECK_LOG_THROW(it == perRenderMaskData.end(), "AssetBuffer::getTypeBuffer() attempting to get a buffer for nonexisting render mask");
  return it->second.typeBuffer;
}
std::shared_ptr<StorageBuffer<AssetLodDefinition>> AssetBuffer::getLodBuffer(uint32_t renderMask)
{
  auto it = perRenderMaskData.find(renderMask);
  CHECK_LOG_THROW(it == perRenderMaskData.end(), "AssetBuffer::getLodBuffer() attempting to get a buffer for nonexisting render mask");
  return it->second.lodBuffer;
}
std::shared_ptr<StorageBuffer<AssetGeometryDefinition>> AssetBuffer::getGeomBuffer(uint32_t renderMask)
{
  auto it = perRenderMaskData.find(renderMask);
  CHECK_LOG_THROW(it == perRenderMaskData.end(), "AssetBuffer::getGeomBuffer() attempting to get a buffer for nonexisting render mask");
  return it->second.geomBuffer;

}

void AssetBuffer::prepareDrawIndexedIndirectCommandBuffer(uint32_t renderMask, std::vector<DrawIndexedIndirectCommand>& resultBuffer, std::vector<uint32_t>& resultGeomToType) const
{
  resultBuffer.resize(0);
  resultGeomToType.resize(0);
  std::vector<InternalGeometryDefinition> geomDefinitions;
  for (const auto& gd : geometryDefinitions)
  {
    if (gd.renderMask == renderMask)
      geomDefinitions.push_back(gd);
  }

  std::sort(geomDefinitions.begin(), geomDefinitions.end(), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs){ if (lhs.typeID != rhs.typeID) return lhs.typeID < rhs.typeID; return lhs.lodID < rhs.lodID; });

  VkDeviceSize                       verticesSoFar = 0;
  VkDeviceSize                       indicesSoFar = 0;
  VkDeviceSize                       indexBufferSize = 0;

  std::vector<AssetLodDefinition>      assetLods;
  std::vector<AssetGeometryDefinition> assetGeometries;
  for (uint32_t t = 0; t < typeDefinitions.size(); ++t)
  {
    auto typePair = std::equal_range(geomDefinitions.begin(), geomDefinitions.end(), InternalGeometryDefinition(t, 0, 0, 0, 0), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs){return lhs.typeID < rhs.typeID; });
    for (uint32_t l = 0; l<lodDefinitions[t].size(); ++l)
    {
      auto lodPair = std::equal_range(typePair.first, typePair.second, InternalGeometryDefinition(t, l, 0, 0, 0), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs){return lhs.lodID < rhs.lodID; });
      if (lodPair.first != lodPair.second)
      {
        for (auto it = lodPair.first; it != lodPair.second; ++it)
        {
          uint32_t indexCount   = assets[it->assetIndex]->geometries[it->geometryIndex].getIndexCount();
          uint32_t firstIndex   = indicesSoFar;
          uint32_t vertexOffset = verticesSoFar;
          resultBuffer.push_back(DrawIndexedIndirectCommand(indexCount, 0, firstIndex, vertexOffset, 0));
          resultGeomToType.push_back(t);

          verticesSoFar += assets[it->assetIndex]->geometries[it->geometryIndex].getVertexCount();
          indicesSoFar  += indexCount;

        }
      }
    }
  }
}

}