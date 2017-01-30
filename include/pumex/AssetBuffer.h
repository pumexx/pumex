#pragma once
#include <map>
#include <algorithm>
#include <pumex/Export.h>
#include <pumex/Asset.h>
#include <pumex/BoundingBox.h>
#include <pumex/Pipeline.h>

namespace pumex
{

class Device;
class CommandPool;
class CommandBuffer;

struct PUMEX_EXPORT AssetTypeDefinition
{
  AssetTypeDefinition()
  {
  }
  AssetTypeDefinition(const BoundingBox& bb)
    : bbMin{ bb.bbMin.x, bb.bbMin.y, bb.bbMin.z, 1.0f }, bbMax{ bb.bbMax.x, bb.bbMax.y, bb.bbMax.z, 1.0f }
  {
  }
  glm::vec4   bbMin;           // we use vec4 for bounding box storing because of std430
  glm::vec4   bbMax;
  uint32_t    lodFirst   = 0;  // used internally
  uint32_t    lodSize    = 0;  // used internally
  uint32_t    std430pad0;
  uint32_t    std430pad1;
};

struct PUMEX_EXPORT AssetLodDefinition
{
  AssetLodDefinition(float minval, float maxval)
    : minDistance{ glm::min(minval, maxval) }, maxDistance{ glm::max(minval, maxval) }
  {
  }
  inline bool active(float distance) const
  {
    return distance >= minDistance && distance < maxDistance;
  }
  uint32_t geomFirst   = 0; // used internally
  uint32_t geomSize    = 0; // used internally
  float    minDistance = 0.0f;
  float    maxDistance = 0.0f;
};

struct PUMEX_EXPORT AssetGeometryDefinition
{
  AssetGeometryDefinition(uint32_t ic, uint32_t fi, uint32_t vo)
    : indexCount{ ic }, firstIndex{ fi }, vertexOffset{vo}
  {
  }
  uint32_t indexCount   = 0;
  uint32_t firstIndex   = 0;
  uint32_t vertexOffset = 0;
//  uint32_t padding      = 0;
};

struct PUMEX_EXPORT DrawIndexedIndirectCommand
{
  DrawIndexedIndirectCommand() = default;
  DrawIndexedIndirectCommand(uint32_t ic, uint32_t inc, uint32_t fi, uint32_t vo, uint32_t fin)
    : indexCount{ic}, instanceCount{inc}, firstIndex{fi}, vertexOffset{vo}, firstInstance{fin}
  {
  }

  uint32_t indexCount    = 0;
  uint32_t instanceCount = 0;
  uint32_t firstIndex    = 0;
  uint32_t vertexOffset  = 0;
  uint32_t firstInstance = 0;
};

struct AssetKey
{
  AssetKey(uint32_t t, uint32_t l)
    : typeID{ t }, lodID{l}
  {
  }
  uint32_t typeID;
  uint32_t lodID;
};

inline bool operator<(const AssetKey& lhs, const AssetKey& rhs)
{
  if (lhs.typeID!=rhs.typeID)
    return lhs.typeID < rhs.typeID;
  return lhs.lodID < rhs.lodID;
}

// Descriptor sets will use this class to access meta buffers held by AssetBuffer ( this class is a kind of adapter... )
class AssetBufferDescriptorSetSource;

// AssetBuffer is a class that holds all assets in a single place in memory.
// Each asset may have different set of render aspects ( normal rendering, transluency, lights, etc ) defined by render mask.
// Each render aspect may use different shaders with different vertex semantic in its geometries.
// First you must define an object type by calling to registerType()
// Then for that type you register Assets as different LODs. Each asset has skeletons, geometries, materials, textures etc.
// Materials and textures are treated in different class called MaterialSet.
// To bind AssetBuffer to vulkan you may use bindVertexIndexBuffer().
// Each render aspect has its own vertex and index buffers, so you are able to use different shaders to draw to different subpasses if you need this.
// Then you are ready to draw objects ( single object may be drawn using drawObject() method, but AssetBuffer was created with
// massive instanced rendering in mind - check crowd and pumexgpucull examples on how to achieve this.
class PUMEX_EXPORT AssetBuffer
{
public:
  explicit AssetBuffer();
  AssetBuffer(const AssetBuffer&)            = delete;
  AssetBuffer& operator=(const AssetBuffer&) = delete;
  virtual ~AssetBuffer();

  void     registerVertexSemantic( uint32_t renderMask, const std::vector<pumex::VertexSemantic>& semantic);
  uint32_t registerType(const std::string& typeName, const AssetTypeDefinition& tdef);
  uint32_t registerObjectLOD( uint32_t typeID, std::shared_ptr<pumex::Asset> asset, const AssetLodDefinition& ldef );
  uint32_t getTypeID(const std::string& typeName) const;
  std::string getTypeName( uint32_t typeID ) const;
  uint32_t getLodID(uint32_t typeID, float distance) const;
  std::shared_ptr<Asset> getAsset(uint32_t typeID, uint32_t lodID);
  inline uint32_t getNumTypesID() const;
  
  void validate(std::shared_ptr<pumex::Device> device, bool useStaging, std::shared_ptr<pumex::CommandPool> commandPool, VkQueue queue = VK_NULL_HANDLE);

  void cmdBindVertexIndexBuffer(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> commandBuffer, uint32_t renderMask, uint32_t vertexBinding = 0) const;
  void cmdDrawObject(std::shared_ptr<pumex::Device> device, std::shared_ptr<pumex::CommandBuffer> commandBuffer, uint32_t renderMask, uint32_t typeID, uint32_t firstInstance, float distanceToViewer) const;

  inline std::shared_ptr<AssetBufferDescriptorSetSource> getTypeBufferDescriptorSetSource(uint32_t renderMask);
  inline std::shared_ptr<AssetBufferDescriptorSetSource> getLODBufferDescriptorSetSource(uint32_t renderMask);
  inline std::shared_ptr<AssetBufferDescriptorSetSource> getGeometryBufferDescriptorSetSource(uint32_t renderMask);

  void prepareDrawIndexedIndirectCommandBuffer(uint32_t renderMask, std::vector<DrawIndexedIndirectCommand>& resultBuffer, std::vector<uint32_t>& resultGeomToType) const;

  friend class AssetBufferDescriptorSetSource;
private:
  struct InternalGeometryDefinition
  {
    InternalGeometryDefinition(uint32_t tid, uint32_t lid, uint32_t rm, uint32_t ai, uint32_t gi)
      : typeID{tid}, lodID{lid}, renderMask{rm}, assetIndex{ai}, geometryIndex{gi}
    {
    }

    uint32_t typeID;
    uint32_t lodID;
    uint32_t renderMask;
    uint32_t assetIndex;
    uint32_t geometryIndex;
  };

  struct PerDeviceData
  {
    PerDeviceData()
    {
    }
    struct VertexIndexMetaBuffers
    {
      VertexIndexMetaBuffers()
      {
      }
      VertexIndexMetaBuffers(VkBuffer v, VkBuffer i, VkDeviceMemory m)
        : vertexBuffer{ v }, indexBuffer{ i }, bufferMemory{ m }
      {
      }
      void deleteBuffers(VkDevice device)
      {
        if (typeBuffer != VK_NULL_HANDLE)
          vkDestroyBuffer(device, typeBuffer, nullptr);
        if (lodBuffer != VK_NULL_HANDLE)
          vkDestroyBuffer(device, lodBuffer, nullptr);
        if (geomBuffer != VK_NULL_HANDLE)
          vkDestroyBuffer(device, geomBuffer, nullptr);
        if (vertexBuffer != VK_NULL_HANDLE)
          vkDestroyBuffer(device, vertexBuffer, nullptr);
        if (indexBuffer != VK_NULL_HANDLE)
          vkDestroyBuffer(device, indexBuffer, nullptr);
        if (bufferMemory != VK_NULL_HANDLE)
          vkFreeMemory(device, bufferMemory, nullptr);
        typeBuffer   = VK_NULL_HANDLE;
        lodBuffer    = VK_NULL_HANDLE;
        geomBuffer   = VK_NULL_HANDLE;
        vertexBuffer = VK_NULL_HANDLE;
        indexBuffer  = VK_NULL_HANDLE;
        bufferMemory = VK_NULL_HANDLE;
      }
      VkBuffer       typeBuffer   = VK_NULL_HANDLE;
      VkBuffer       lodBuffer    = VK_NULL_HANDLE;
      VkBuffer       geomBuffer   = VK_NULL_HANDLE;
      VkBuffer       vertexBuffer = VK_NULL_HANDLE;
      VkBuffer       indexBuffer  = VK_NULL_HANDLE;
      VkDeviceMemory bufferMemory = VK_NULL_HANDLE;

      std::vector<AssetTypeDefinition>     assetTypes;
      std::vector<AssetLodDefinition>      assetLods;
      std::vector<AssetGeometryDefinition> assetGeometries;
    };
    std::map<uint32_t, VertexIndexMetaBuffers> vertexIndexBuffers;
    bool                                       buffersDirty = true;
  };
  std::map<uint32_t,std::vector<pumex::VertexSemantic>> semantics;
  std::vector<std::string>                              typeNames;
  std::map<std::string, uint32_t>                       invTypeNames;
  std::vector<AssetTypeDefinition>                      typeDefinitions;
  std::vector<std::vector<AssetLodDefinition>>          lodDefinitions;
  std::vector<InternalGeometryDefinition>               geometryDefinitions;
  std::vector<std::shared_ptr<Asset>>                   assets; // asset buffer owns assets
  std::map<AssetKey, std::shared_ptr<Asset>>            assetMapping; 
  std::map<VkDevice, PerDeviceData> perDeviceData;

  std::map<uint32_t, std::shared_ptr<AssetBufferDescriptorSetSource>> typeBufferDescriptorSetValue;
  std::map<uint32_t, std::shared_ptr<AssetBufferDescriptorSetSource>> lodBufferDescriptorSetValue;
  std::map<uint32_t, std::shared_ptr<AssetBufferDescriptorSetSource>> geomBufferDescriptorSetValue;
};


// Descriptor sets will use this class to access meta buffers held by AssetBuffer ( this class is a kind of adapter... )
class PUMEX_EXPORT AssetBufferDescriptorSetSource : public DescriptorSetSource
{
public:
  enum BufferType{ TypeBuffer, LodBuffer, GeometryBuffer };
  AssetBufferDescriptorSetSource(AssetBuffer* owner, uint32_t renderMask, BufferType bufferType);
  DescriptorSetValue getDescriptorSetValue(VkDevice device) const override;
private:
  AssetBuffer* owner;
  uint32_t     renderMask;
  BufferType   bufferType;
};

uint32_t AssetBuffer::getNumTypesID() const
{
  return typeNames.size();
}

std::shared_ptr<AssetBufferDescriptorSetSource> AssetBuffer::getTypeBufferDescriptorSetSource(uint32_t renderMask) 
{
  auto it = typeBufferDescriptorSetValue.find(renderMask);
  if (it == typeBufferDescriptorSetValue.end())
    it = typeBufferDescriptorSetValue.insert({ renderMask, std::make_shared<AssetBufferDescriptorSetSource>(this, renderMask, AssetBufferDescriptorSetSource::TypeBuffer) }).first;
  return it->second;
}
std::shared_ptr<AssetBufferDescriptorSetSource> AssetBuffer::getLODBufferDescriptorSetSource(uint32_t renderMask)
{
  auto it = lodBufferDescriptorSetValue.find(renderMask);
  if (it == lodBufferDescriptorSetValue.end())
    it = lodBufferDescriptorSetValue.insert({ renderMask, std::make_shared<AssetBufferDescriptorSetSource>(this, renderMask, AssetBufferDescriptorSetSource::LodBuffer) }).first;
  return it->second;
}
std::shared_ptr<AssetBufferDescriptorSetSource> AssetBuffer::getGeometryBufferDescriptorSetSource(uint32_t renderMask)
{
  auto it = geomBufferDescriptorSetValue.find(renderMask);
  if (it == geomBufferDescriptorSetValue.end())
    it = geomBufferDescriptorSetValue.insert({ renderMask, std::make_shared<AssetBufferDescriptorSetSource>(this, renderMask, AssetBufferDescriptorSetSource::GeometryBuffer) }).first;
  return it->second;
}

}
