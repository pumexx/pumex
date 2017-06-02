#include <pumex/AssetBuffer.h>
#include <cstring>
#include <set>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

namespace pumex
{

AssetBuffer::AssetBuffer()
{
  typeNames.push_back("<null>");
  invTypeNames.insert({ "<null>", 0 });
  typeDefinitions.push_back(AssetTypeDefinition());
  lodDefinitions.push_back(std::vector<AssetLodDefinition>());
}

AssetBuffer::~AssetBuffer()
{
  for ( auto& pdd : perDeviceData)
    for ( auto& vib : pdd.second.vertexIndexBuffers )
      vib.second.deleteBuffers(pdd.first);
}

void AssetBuffer::registerVertexSemantic(uint32_t renderMask, const std::vector<pumex::VertexSemantic>& semantic)
{
  semantics[renderMask] = semantic;
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
  return typeID;
}

uint32_t AssetBuffer::registerObjectLOD(uint32_t typeID, std::shared_ptr<pumex::Asset> asset, const AssetLodDefinition& ldef)
{
  if (typeID == 0 || typeNames.size()<typeID)
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

void AssetBuffer::validate(std::shared_ptr<pumex::Device> device, bool useStaging, std::shared_ptr<pumex::CommandPool> commandPool, VkQueue queue)
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
    pddit = perDeviceData.insert({ device->device, PerDeviceData() }).first;
  if (!pddit->second.buffersDirty)
    return;
  for (auto& vib : pddit->second.vertexIndexBuffers)
    vib.second.deleteBuffers(pddit->first);
  pddit->second.vertexIndexBuffers.clear();

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
    // Sort geometries according to typeID and lodID
    std::sort(gd.second.begin(), gd.second.end(), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs){ if (lhs.typeID != rhs.typeID) return lhs.typeID < rhs.typeID; return lhs.lodID < rhs.lodID; });

    VkDeviceSize                       verticesSoFar    = 0;
    VkDeviceSize                       indicesSoFar     = 0;
    VkDeviceSize                       indexBufferSize  = 0;
    std::vector<float>                 vertexBuffer; // vertices converted to required semantic;
    std::vector<uint32_t>              geometryOrder;
    std::vector<pumex::VertexSemantic> requiredSemantic;
    auto sid = semantics.find(gd.first); // get required semantic
    if (sid != semantics.end())
      requiredSemantic = sid->second;

    std::vector<AssetTypeDefinition>     assetTypes = typeDefinitions;
    std::vector<AssetLodDefinition>      assetLods;
    std::vector<AssetGeometryDefinition> assetGeometries;
    for (uint32_t t = 0; t < assetTypes.size(); ++t)
    {
      auto typePair = std::equal_range(gd.second.begin(), gd.second.end(), InternalGeometryDefinition(t, 0, 0, 0, 0), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs){return lhs.typeID < rhs.typeID; });
      assetTypes[t].lodFirst = assetLods.size();
      for (uint32_t l = 0; l<lodDefinitions[t].size(); ++l)
      {
        auto lodPair = std::equal_range(typePair.first, typePair.second, InternalGeometryDefinition(t, l, 0, 0, 0), [](const InternalGeometryDefinition& lhs, const InternalGeometryDefinition& rhs){return lhs.lodID < rhs.lodID; });
        if (lodPair.first != lodPair.second)
        {
          AssetLodDefinition lodDef = lodDefinitions[t][l];
          lodDef.geomFirst = assetGeometries.size();
          for (auto it = lodPair.first; it != lodPair.second; ++it)
          {
            // if required semantic is not defined then take semantic from first spotted geometry
            if (requiredSemantic.empty())
              requiredSemantic = assets[it->assetIndex]->geometries[it->geometryIndex].semantic;

            uint32_t indexCount   = assets[it->assetIndex]->geometries[it->geometryIndex].getIndexCount();
            uint32_t firstIndex   = indicesSoFar;
            uint32_t vertexOffset = verticesSoFar;
            assetGeometries.push_back(AssetGeometryDefinition(indexCount, firstIndex, vertexOffset));

            // calculating buffer sizes etc
            verticesSoFar   += assets[it->assetIndex]->geometries[it->geometryIndex].getVertexCount();
            indicesSoFar    += indexCount;
            indexBufferSize += assets[it->assetIndex]->geometries[it->geometryIndex].getIndexSize();
            pumex::copyAndConvertVertices(vertexBuffer, requiredSemantic, assets[it->assetIndex]->geometries[it->geometryIndex].vertices, assets[it->assetIndex]->geometries[it->geometryIndex].semantic);

            geometryOrder.push_back(std::distance(gd.second.begin(), it));
          }
          lodDef.geomSize = assetGeometries.size() - lodDef.geomFirst;
          assetLods.push_back(lodDef);
        }
      }
      assetTypes[t].lodSize = assetLods.size() - assetTypes[t].lodFirst;
    }

    auto vibit = pddit->second.vertexIndexBuffers.find(gd.first);
    if (vibit == pddit->second.vertexIndexBuffers.end())
      vibit = pddit->second.vertexIndexBuffers.insert({ gd.first, PerDeviceData::VertexIndexMetaBuffers() }).first;
    vibit->second.assetTypes      = assetTypes;
    vibit->second.assetLods       = assetLods;
    vibit->second.assetGeometries = assetGeometries;

    // send all vertex and index data to GPU memory, use staging buffers if requested
    if (useStaging)
    {
      // Create staging buffers
      VkDeviceMemory stagingMemory;
      std::vector<NBufferMemory> stagingBuffers = 
      {
        { VK_BUFFER_USAGE_TRANSFER_SRC_BIT, assetTypes.size() * sizeof(AssetTypeDefinition), nullptr },
        { VK_BUFFER_USAGE_TRANSFER_SRC_BIT, assetLods.size() * sizeof(AssetLodDefinition), nullptr },
        { VK_BUFFER_USAGE_TRANSFER_SRC_BIT, assetGeometries.size() * sizeof(AssetGeometryDefinition), nullptr },
        { VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vertexBuffer.size() * sizeof(float), nullptr },
        { VK_BUFFER_USAGE_TRANSFER_SRC_BIT, indexBufferSize, nullptr } 
      };
      createBuffers(device, stagingBuffers, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &stagingMemory);
    
      // and copy data ( meta, vertices and indices ) from each mesh into buffers
      uint8_t* mapAddress;

      // types go to first buffer
      VK_CHECK_LOG_THROW(vkMapMemory(pddit->first, stagingMemory, stagingBuffers[0].memoryOffset, stagingBuffers[0].size, 0, (void**)&mapAddress), "Cannot map memory");
      std::memcpy(mapAddress, assetTypes.data(), stagingBuffers[0].size);
      vkUnmapMemory(pddit->first, stagingMemory);

      // lods go to second buffer
      VK_CHECK_LOG_THROW(vkMapMemory(pddit->first, stagingMemory, stagingBuffers[1].memoryOffset, stagingBuffers[1].size, 0, (void**)&mapAddress), "Cannot map memory");
      std::memcpy(mapAddress, assetLods.data(), stagingBuffers[1].size);
      vkUnmapMemory(pddit->first, stagingMemory);

      // geometry definitions go to third buffer
      VK_CHECK_LOG_THROW(vkMapMemory(pddit->first, stagingMemory, stagingBuffers[2].memoryOffset, stagingBuffers[2].size, 0, (void**)&mapAddress), "Cannot map memory");
      std::memcpy(mapAddress, assetGeometries.data(), stagingBuffers[2].size);
      vkUnmapMemory(pddit->first, stagingMemory);

      // vertices go to fourth buffer
      VK_CHECK_LOG_THROW(vkMapMemory(pddit->first, stagingMemory, stagingBuffers[3].memoryOffset, stagingBuffers[3].size, 0, (void**)&mapAddress), "Cannot map memory");
      std::memcpy(mapAddress, vertexBuffer.data(), stagingBuffers[3].size);
      vkUnmapMemory(pddit->first, stagingMemory);

      // and indices go to last fifth buffer
      VK_CHECK_LOG_THROW(vkMapMemory(pddit->first, stagingMemory, stagingBuffers[4].memoryOffset, stagingBuffers[4].size, 0, (void**)&mapAddress), "Cannot map memory");
      VkDeviceSize copyOffset = 0;
      for (uint32_t i = 0; i<geometryOrder.size(); i++)
      {
        const auto& igd = gd.second[geometryOrder[i]];
        const auto& indices = assets[igd.assetIndex]->geometries[igd.geometryIndex].indices;
        size_t dataToCopy = indices.size() * sizeof(uint32_t);
        std::memcpy(mapAddress + copyOffset, indices.data(), dataToCopy);
        copyOffset += dataToCopy;
      }
      vkUnmapMemory(pddit->first, stagingMemory);
    
      // Create device local buffers
      std::vector<NBufferMemory> targetBuffers =
      {
        { VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, assetTypes.size() * sizeof(AssetTypeDefinition), nullptr },
        { VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, assetLods.size() * sizeof(AssetLodDefinition), nullptr },
        { VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, assetGeometries.size() * sizeof(AssetGeometryDefinition), nullptr },
        { VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vertexBuffer.size() * sizeof(float), nullptr },
        { VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, indexBufferSize, nullptr }
      };
    
      createBuffers(device, targetBuffers, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vibit->second.bufferMemory);
      vibit->second.typeBuffer   = targetBuffers[0].buffer;
      vibit->second.lodBuffer    = targetBuffers[1].buffer;
      vibit->second.geomBuffer   = targetBuffers[2].buffer;
      vibit->second.vertexBuffer = targetBuffers[3].buffer;
      vibit->second.indexBuffer  = targetBuffers[4].buffer;
    
      // Copy staging buffers to target buffers
      auto staggingCommandBuffer = device->beginSingleTimeCommands(commandPool);
    
      VkBufferCopy copyRegion{};
      for (uint32_t i=0; i<5; ++i)
      {
        copyRegion.size = stagingBuffers[i].size;
        staggingCommandBuffer->cmdCopyBuffer(stagingBuffers[i].buffer, targetBuffers[i].buffer, copyRegion);
      }
      device->endSingleTimeCommands(staggingCommandBuffer, queue);
      destroyBuffers(device, stagingBuffers, stagingMemory);
    }
    else
    {
      // create target buffers
      std::vector<NBufferMemory> targetBuffers =
      {
        { VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, assetTypes.size() * sizeof(AssetTypeDefinition), nullptr },
        { VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, assetLods.size() * sizeof(AssetLodDefinition), nullptr },
        { VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, assetGeometries.size() * sizeof(AssetGeometryDefinition), nullptr },
        { VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vertexBuffer.size() * sizeof(float), nullptr },
        { VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, indexBufferSize, nullptr }
      };

      createBuffers(device, targetBuffers, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &vibit->second.bufferMemory);
      vibit->second.typeBuffer   = targetBuffers[0].buffer;
      vibit->second.lodBuffer    = targetBuffers[1].buffer;
      vibit->second.geomBuffer   = targetBuffers[2].buffer;
      vibit->second.vertexBuffer = targetBuffers[3].buffer;
      vibit->second.indexBuffer  = targetBuffers[4].buffer;
    
      // and copy data ( vertices and indices ) from each mesh into buffers
      uint8_t* mapAddress;
      // types go to first buffer
      VK_CHECK_LOG_THROW(vkMapMemory(pddit->first, vibit->second.bufferMemory, targetBuffers[0].memoryOffset, targetBuffers[0].size, 0, (void**)&mapAddress), "Cannot map memory");
      std::memcpy(mapAddress, assetTypes.data(), targetBuffers[0].size);
      vkUnmapMemory(pddit->first, vibit->second.bufferMemory);

      // lods go to second buffer
      VK_CHECK_LOG_THROW(vkMapMemory(pddit->first, vibit->second.bufferMemory, targetBuffers[1].memoryOffset, targetBuffers[1].size, 0, (void**)&mapAddress), "Cannot map memory");
      std::memcpy(mapAddress, assetLods.data(), targetBuffers[1].size);
      vkUnmapMemory(pddit->first, vibit->second.bufferMemory);

      // geometry definitions go to third buffer
      VK_CHECK_LOG_THROW(vkMapMemory(pddit->first, vibit->second.bufferMemory, targetBuffers[2].memoryOffset, targetBuffers[2].size, 0, (void**)&mapAddress), "Cannot map memory");
      std::memcpy(mapAddress, assetGeometries.data(), targetBuffers[2].size);
      vkUnmapMemory(pddit->first, vibit->second.bufferMemory);

      // vertices go to fourth buffer
      VK_CHECK_LOG_THROW(vkMapMemory(pddit->first, vibit->second.bufferMemory, targetBuffers[3].memoryOffset, targetBuffers[3].size, 0, (void**)&mapAddress), "Cannot map memory");
      std::memcpy(mapAddress, vertexBuffer.data(), targetBuffers[3].size);
      vkUnmapMemory(pddit->first, vibit->second.bufferMemory);

      // and indices go to last fifth buffer
      VK_CHECK_LOG_THROW(vkMapMemory(pddit->first, vibit->second.bufferMemory, targetBuffers[4].memoryOffset, targetBuffers[4].size, 0, (void**)&mapAddress), "Cannot map memory");
      VkDeviceSize copyOffset = 0;
      for (uint32_t i = 0; i<geometryOrder.size(); i++)
      {
        const auto& igd = gd.second[geometryOrder[i]];
        const auto& indices = assets[igd.assetIndex]->geometries[igd.geometryIndex].indices;
        size_t dataToCopy = indices.size() * sizeof(uint32_t);
        std::memcpy(mapAddress + copyOffset, indices.data(), dataToCopy);
        copyOffset += dataToCopy;
      }
      vkUnmapMemory(pddit->first, vibit->second.bufferMemory);
    }
    auto tbd = typeBufferDescriptorSetValue.find(gd.first);
    if (tbd != typeBufferDescriptorSetValue.end())  tbd->second->notifyDescriptorSets();

    auto lbd = lodBufferDescriptorSetValue.find(gd.first);
    if (lbd != lodBufferDescriptorSetValue.end())  lbd->second->notifyDescriptorSets();

    auto gbd = geomBufferDescriptorSetValue.find(gd.first);
    if (gbd != geomBufferDescriptorSetValue.end())  gbd->second->notifyDescriptorSets();
  }
  pddit->second.buffersDirty = false;
}

void AssetBuffer::cmdBindVertexIndexBuffer(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> commandBuffer, uint32_t renderMask, uint32_t vertexBinding) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
  {
    LOG_WARNING << "AssetBuffer::bindVertexIndexBuffer() used with device that does not have buffer defined" << std::endl;
    return;
  }
  auto vib = pddit->second.vertexIndexBuffers.find(renderMask);
  if (vib == pddit->second.vertexIndexBuffers.end())
  {
    LOG_WARNING << "AssetBuffer::bindVertexIndexBuffer() does not have this render mask defined" << std::endl;
    return;
  }
  VkBuffer vBuffer = vib->second.vertexBuffer;
  VkBuffer iBuffer = vib->second.indexBuffer;
  VkDeviceSize offsets = 0;
  vkCmdBindVertexBuffers(commandBuffer->getHandle(), vertexBinding, 1, &vBuffer, &offsets);
  vkCmdBindIndexBuffer(commandBuffer->getHandle(), iBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void AssetBuffer::cmdDrawObject(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> commandBuffer, uint32_t renderMask, uint32_t typeID, uint32_t firstInstance, float distanceToViewer) const
{
  auto pddit = perDeviceData.find(device->device);
  if (pddit == perDeviceData.end())
  {
    LOG_WARNING << "AssetBuffer::drawObject() used with device that does not have buffer defined" << std::endl;
    return;
  }
  auto vib = pddit->second.vertexIndexBuffers.find(renderMask);
  if (vib == pddit->second.vertexIndexBuffers.end())
  {
    LOG_WARNING << "AssetBuffer::drawObject does not have this render mask defined" << std::endl;
    return;
  }

  uint32_t lodFirst = vib->second.assetTypes[typeID].lodFirst;
  uint32_t lodSize  = vib->second.assetTypes[typeID].lodSize;
  for (unsigned int l = lodFirst; l < lodFirst + lodSize; ++l)
  {
    if (vib->second.assetLods[l].active(distanceToViewer))
    {
      uint32_t geomFirst = vib->second.assetLods[l].geomFirst;
      uint32_t geomSize  = vib->second.assetLods[l].geomSize;
      for (uint32_t g = geomFirst; g < geomFirst + geomSize; ++g)
      {
        uint32_t indexCount   = vib->second.assetGeometries[g].indexCount;
        uint32_t firstIndex   = vib->second.assetGeometries[g].firstIndex;
        uint32_t vertexOffset = vib->second.assetGeometries[g].vertexOffset;
        commandBuffer->cmdDrawIndexed(indexCount, 1, firstIndex, vertexOffset, firstInstance);
      }
    }
  }
}

AssetBufferDescriptorSetSource::AssetBufferDescriptorSetSource(AssetBuffer* o, uint32_t rm, BufferType bt)
  : owner{ o }, renderMask{ rm }, bufferType{bt}
{
}

void AssetBufferDescriptorSetSource::getDescriptorSetValues(VkDevice device, std::vector<DescriptorSetValue>& values) const
{
  CHECK_LOG_THROW(owner == nullptr,"AssetBufferDescriptorSetSource::getDescriptorSetValue() : owner not defined");
  auto pddit = owner->perDeviceData.find(device);
  CHECK_LOG_THROW(pddit == owner->perDeviceData.end(), "AssetBufferDescriptorSetSource::getDescriptorBufferInfo() : AssetBuffer not validated for device " << device);
  auto vibit = pddit->second.vertexIndexBuffers.find(renderMask);
  CHECK_LOG_THROW(vibit == pddit->second.vertexIndexBuffers.end(), "AssetBufferDescriptorSetSource::getDescriptorBufferInfo() : AssetBuffer does not have this render mask defined " << renderMask);
  switch (bufferType)
  {
    case AssetBufferDescriptorSetSource::TypeBuffer :
      values.push_back( DescriptorSetValue(vibit->second.typeBuffer, 0, vibit->second.assetTypes.size() * sizeof(AssetTypeDefinition)) );
      break;
    case AssetBufferDescriptorSetSource::LodBuffer:
      values.push_back( DescriptorSetValue(vibit->second.lodBuffer, 0, vibit->second.assetLods.size() * sizeof(AssetLodDefinition)) );
      break;
    case AssetBufferDescriptorSetSource::GeometryBuffer:
      values.push_back( DescriptorSetValue(vibit->second.geomBuffer, 0, vibit->second.assetGeometries.size() * sizeof(AssetGeometryDefinition)) );
      break;
  }
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