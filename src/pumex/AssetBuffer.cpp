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

#include <pumex/AssetBuffer.h>
#include <set>
#include <iterator>
#include <pumex/Device.h>
#include <pumex/Node.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/RenderContext.h>
#include <pumex/MemoryBuffer.h>
#include <pumex/Command.h>
#include <pumex/utils/Log.h>

namespace pumex
{

AssetBuffer::AssetBuffer(const std::vector<AssetBufferVertexSemantics>& vertexSemantics, std::shared_ptr<DeviceMemoryAllocator> bufferAllocator, std::shared_ptr<DeviceMemoryAllocator> vertexIndexAllocator)
{
  for (const auto& vs : vertexSemantics)
  {
    semantics[vs.renderMask]         = vs.vertexSemantic;
    perRenderMaskData[vs.renderMask] = PerRenderMaskData(bufferAllocator, vertexIndexAllocator);
  }

  // create "null" type
  typeDefinitions.push_back(AssetTypeDefinition());
  lodDefinitions.push_back(std::vector<AssetLodDefinition>());
}

AssetBuffer::~AssetBuffer()
{
}

void AssetBuffer::registerType(uint32_t typeID, const AssetTypeDefinition& tdef)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (typeDefinitions.size() < typeID + 1)
  {
    typeDefinitions.resize(typeID + 1, AssetTypeDefinition());
    lodDefinitions.resize(typeID + 1, std::vector<AssetLodDefinition>());
  }
  typeDefinitions[typeID] = tdef;
  lodDefinitions[typeID] = std::vector<AssetLodDefinition>();
  geometryDefinitions.erase(std::remove_if(begin(geometryDefinitions), end(geometryDefinitions), [typeID](const InternalGeometryDefinition& gdef) { return gdef.typeID == typeID; }), end(geometryDefinitions));
  valid = false;
  invalidateNodeOwners();
}

uint32_t AssetBuffer::registerObjectLOD(uint32_t typeID, const AssetLodDefinition& ldef, std::shared_ptr<Asset> asset )
{
  CHECK_LOG_THROW(typeID >= lodDefinitions.size(), "AssetBuffer::registerObjectLOD() : LOD definition out of bounds");
  std::lock_guard<std::mutex> lock(mutex);

  uint32_t lodID = lodDefinitions[typeID].size();
  lodDefinitions[typeID].push_back(ldef);

  // check if this asset has been registered already
  auto ait = std::find_if(begin(assets), end(assets), [&asset](std::shared_ptr<Asset> a) { return a.get() == asset.get(); });
  uint32_t assetIndex = (ait == end(assets)) ? assets.size() : std::distance(begin(assets), ait);
  // register asset when not registered already
  if (ait == end(assets))
    assets.push_back(asset);
  assetMapping.insert({ AssetKey(typeID,lodID), asset });

  for (uint32_t i = 0; i<asset->geometries.size(); ++i)
    geometryDefinitions.push_back(InternalGeometryDefinition(typeID, lodID, asset->geometries[i].renderMask, assetIndex, i));
  valid = false;
  invalidateNodeOwners();
  return lodID;
}

uint32_t AssetBuffer::getLodID(uint32_t typeID, float distance) const
{
  CHECK_LOG_THROW(typeID >= lodDefinitions.size(), "AssetBuffer::getLodID() : LOD definition out of bounds");
  for (uint32_t i = 0; i < lodDefinitions[typeID].size(); ++i)
    if (lodDefinitions[typeID][i].active(distance))
      return i;
  return std::numeric_limits<uint32_t>::max();
}

std::shared_ptr<Asset> AssetBuffer::getAsset(uint32_t typeID, uint32_t lodID)
{
  auto it = assetMapping.find(AssetKey(typeID, lodID));
  if (it != end(assetMapping))
    return it->second;
  return std::shared_ptr<Asset>();
}

std::vector<uint32_t> AssetBuffer::getRenderMasks() const
{
  std::vector<uint32_t> results;
  for (auto& prm : perRenderMaskData)
    results.push_back(prm.first);
  return results;
}

bool AssetBuffer::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  bool result = false;
  if (!valid)
  {
    // divide geometries according to renderMasks
    std::map<uint32_t, std::vector<InternalGeometryDefinition>> geometryDefinitionsByRenderMask;
    for (const auto& gd : geometryDefinitions)
    {
      auto it = geometryDefinitionsByRenderMask.find(gd.renderMask);
      if (it == end(geometryDefinitionsByRenderMask))
        it = geometryDefinitionsByRenderMask.insert({ gd.renderMask, std::vector<InternalGeometryDefinition>() }).first;
      it->second.push_back(gd);
    }

    for (auto& gd : geometryDefinitionsByRenderMask)
    {
      // only create asset buffers for render masks that have nonempty vertex semantic defined
      auto pdmit = perRenderMaskData.find(gd.first);
      if (pdmit == end(perRenderMaskData))
        continue;
      PerRenderMaskData& rmData = pdmit->second;

      std::vector<VertexSemantic> requiredSemantic;
      auto sit = semantics.find(gd.first);
      if (sit != end(semantics))
        requiredSemantic = sit->second;
      if (requiredSemantic.empty())
        continue;

      // Sort geometries according to typeID and lodID
      std::sort(begin(gd.second), end(gd.second), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs) { if (lhs.typeID != rhs.typeID) return lhs.typeID < rhs.typeID; return lhs.lodID < rhs.lodID; });

      VkDeviceSize     verticesSoFar = 0;
      VkDeviceSize     indicesSoFar = 0;
      rmData.vertices->resize(0);
      rmData.indices->resize(0);

      std::vector<AssetTypeDefinition>     assetTypes = typeDefinitions;
      std::vector<AssetLodDefinition>      assetLods;
      std::vector<AssetGeometryDefinition> assetGeometries;
      for (uint32_t t = 0; t < assetTypes.size(); ++t)
      {
        auto typePair = std::equal_range(begin(gd.second), end(gd.second), InternalGeometryDefinition(t, 0, 0, 0, 0), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs) {return lhs.typeID < rhs.typeID; });
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
              std::copy(begin(indices), end(indices), std::back_inserter(*(rmData.indices)));
            }
            lodDef.geomSize = assetGeometries.size() - lodDef.geomFirst;
            assetLods.push_back(lodDef);
          }
        }
        assetTypes[t].lodSize = assetLods.size() - assetTypes[t].lodFirst;
      }
      rmData.vertexBuffer->invalidateData();
      rmData.indexBuffer->invalidateData();
      (*rmData.aTypes)    = assetTypes;
      (*rmData.aLods)     = assetLods;
      (*rmData.aGeomDefs) = assetGeometries;
      rmData.typeBuffer->invalidateData();
      rmData.lodBuffer->invalidateData();
      rmData.geomBuffer->invalidateData();
    }
    result = true;
  }
  for (auto& prm : perRenderMaskData)
  {
    prm.second.vertexBuffer->validate(renderContext);
    prm.second.indexBuffer->validate(renderContext);
  }
  valid = true;
  return result;
}

void AssetBuffer::cmdBindVertexIndexBuffer(const RenderContext& renderContext, CommandBuffer* commandBuffer, uint32_t renderMask, uint32_t vertexBinding)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto prmit = perRenderMaskData.find(renderMask);
  if (prmit == end(perRenderMaskData))
  {
    LOG_WARNING << "AssetBuffer::bindVertexIndexBuffer() does not have this render mask defined" << std::endl;
    return;
  }
  VkBuffer vBuffer = prmit->second.vertexBuffer->getHandleBuffer(renderContext);
  VkBuffer iBuffer = prmit->second.indexBuffer->getHandleBuffer(renderContext);
  VkDeviceSize offsets = 0;
  vkCmdBindVertexBuffers(commandBuffer->getHandle(), vertexBinding, 1, &vBuffer, &offsets);
  vkCmdBindIndexBuffer(commandBuffer->getHandle(), iBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void AssetBuffer::cmdDrawObject(const RenderContext& renderContext, CommandBuffer* commandBuffer, uint32_t renderMask, uint32_t typeID, uint32_t firstInstance, float distanceToViewer) const
{
  std::lock_guard<std::mutex> lock(mutex);

  auto prmit = perRenderMaskData.find(renderMask);
  if (prmit == end(perRenderMaskData))
  {
    LOG_WARNING << "AssetBuffer::drawObject() does not have this render mask defined" << std::endl;
    return;
  }
  auto& assetTypes      = *prmit->second.aTypes;
  auto& assetLods       = *prmit->second.aLods;
  auto& assetGeometries = *prmit->second.aGeomDefs;

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

void AssetBuffer::cmdDrawObjectsIndirect(const RenderContext& renderContext, CommandBuffer* commandBuffer, std::shared_ptr<Buffer<std::vector<DrawIndexedIndirectCommand>>> drawCommands)
{
  std::lock_guard<std::mutex> lock(mutex);

  auto buffer = drawCommands->getHandleBuffer(renderContext);

  uint32_t drawCount = drawCommands->getData()->size();

  if (renderContext.device->physical.lock()->features.multiDrawIndirect == 1)
    commandBuffer->cmdDrawIndexedIndirect(buffer, 0, drawCount, sizeof(DrawIndexedIndirectCommand));
  else
  {
    for (uint32_t i = 0; i < drawCount; ++i)
      commandBuffer->cmdDrawIndexedIndirect(buffer, 0 + i * sizeof(DrawIndexedIndirectCommand), 1, sizeof(DrawIndexedIndirectCommand));
  }
}

std::shared_ptr<Buffer<std::vector<AssetTypeDefinition>>> AssetBuffer::getTypeBuffer(uint32_t renderMask)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perRenderMaskData.find(renderMask);
  CHECK_LOG_THROW(it == end(perRenderMaskData), "AssetBuffer::getTypeBuffer() attempting to get a buffer for nonexisting render mask");
  return it->second.typeBuffer;
}

std::shared_ptr<Buffer<std::vector<AssetLodDefinition>>> AssetBuffer::getLodBuffer(uint32_t renderMask)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perRenderMaskData.find(renderMask);
  CHECK_LOG_THROW(it == end(perRenderMaskData), "AssetBuffer::getLodBuffer() attempting to get a buffer for nonexisting render mask");
  return it->second.lodBuffer;
}

std::shared_ptr<Buffer<std::vector<AssetGeometryDefinition>>> AssetBuffer::getGeomBuffer(uint32_t renderMask)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perRenderMaskData.find(renderMask);
  CHECK_LOG_THROW(it == end(perRenderMaskData), "AssetBuffer::getGeomBuffer() attempting to get a buffer for nonexisting render mask");
  return it->second.geomBuffer;
}

void AssetBuffer::prepareDrawCommands(uint32_t renderMask, std::vector<DrawIndexedIndirectCommand>& drawCommands, std::vector<uint32_t>& typeOfGeometry) const
{
  drawCommands.resize(0);
  typeOfGeometry.resize(0);
  std::vector<InternalGeometryDefinition> geomDefinitions;
  for (const auto& gd : geometryDefinitions)
  {
    if (gd.renderMask == renderMask)
      geomDefinitions.push_back(gd);
  }

  std::sort(begin(geomDefinitions), end(geomDefinitions), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs){ if (lhs.typeID != rhs.typeID) return lhs.typeID < rhs.typeID; return lhs.lodID < rhs.lodID; });

  VkDeviceSize                       verticesSoFar = 0;
  VkDeviceSize                       indicesSoFar = 0;
  VkDeviceSize                       indexBufferSize = 0;

  std::vector<AssetLodDefinition>      assetLods;
  std::vector<AssetGeometryDefinition> assetGeometries;
  for (uint32_t t = 0; t < typeDefinitions.size(); ++t)
  {
    auto typePair = std::equal_range(begin(geomDefinitions), end(geomDefinitions), InternalGeometryDefinition(t, 0, 0, 0, 0), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs){return lhs.typeID < rhs.typeID; });
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
          drawCommands.push_back(DrawIndexedIndirectCommand(indexCount, 0, firstIndex, vertexOffset, 0));
          typeOfGeometry.push_back(t);

          verticesSoFar += assets[it->assetIndex]->geometries[it->geometryIndex].getVertexCount();
          indicesSoFar  += indexCount;
        }
      }
    }
  }
}

void AssetBuffer::addNodeOwner(std::shared_ptr<Node> node)
{
  if (std::find_if(begin(nodeOwners), end(nodeOwners), [&node](std::weak_ptr<Node> n) { return !n.expired() && n.lock().get() == node.get(); }) == end(nodeOwners))
    nodeOwners.push_back(node);
}

void AssetBuffer::invalidateNodeOwners()
{
  auto eit = std::remove_if(begin(nodeOwners), end(nodeOwners), [](std::weak_ptr<Node> n) { return n.expired();  });
  for (auto it = begin(nodeOwners); it != eit; ++it)
    it->lock()->invalidateNodeAndParents();
  nodeOwners.erase(eit, end(nodeOwners));
}

AssetBuffer::PerRenderMaskData::PerRenderMaskData(std::shared_ptr<DeviceMemoryAllocator> bufferAllocator, std::shared_ptr<DeviceMemoryAllocator> vertexIndexAllocator)
{
  vertices     = std::make_shared<std::vector<float>>();
  indices      = std::make_shared<std::vector<uint32_t>>();
  vertexBuffer = std::make_shared<Buffer<std::vector<float>>>(vertices, vertexIndexAllocator, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, pbPerDevice, swForEachImage);
  indexBuffer  = std::make_shared<Buffer<std::vector<uint32_t>>>(indices, vertexIndexAllocator, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, pbPerDevice, swForEachImage);

  aTypes       = std::make_shared<std::vector<AssetTypeDefinition>>();
  aLods        = std::make_shared<std::vector<AssetLodDefinition>>();
  aGeomDefs    = std::make_shared<std::vector<AssetGeometryDefinition>>();
  typeBuffer   = std::make_shared<Buffer<std::vector<AssetTypeDefinition>>>(aTypes, bufferAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pbPerDevice, swForEachImage);
  lodBuffer    = std::make_shared<Buffer<std::vector<AssetLodDefinition>>>(aLods, bufferAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pbPerDevice, swForEachImage);
  geomBuffer   = std::make_shared<Buffer<std::vector<AssetGeometryDefinition>>>(aGeomDefs, bufferAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pbPerDevice, swForEachImage);
}

}
