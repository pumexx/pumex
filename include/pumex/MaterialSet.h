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

#pragma once
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <vulkan/vulkan.h>
#include <gli/load.hpp>
#include <pumex/Export.h>
#include <pumex/Texture.h>
#include <pumex/Command.h>
#include <pumex/Asset.h>
#include <pumex/Device.h>
#include <pumex/Viewer.h>
#include <pumex/utils/Buffer.h>

namespace pumex
{

// Assimp does not load textures, but only its names and purpose ( diffuse, normal, etc )
// TextureSemantic struct helps to differentiate these purposes and put textures in proper places in MaterialSet
struct TextureSemantic
{
  // you must modify shaders if the number of types defined below has changed. Shaders may use TextureSemanticCount
  enum Type { Diffuse, Specular, Ambient, Emissive, Height, Normals, Shininess, Opacity, Displacement, LightMap, Reflection, TextureSemanticCount };

  TextureSemantic(const Type& t, uint32_t i)
    : type{ t }, index{ i }
  {
  }

  // Texture semantic types taken from assimp
  Type     type;
  uint32_t index;
};

// structs representing different material types and its variants
struct MaterialTypeDefinition
{
  uint32_t variantFirst;
  uint32_t variantSize;
};

struct MaterialVariantDefinition
{
  uint32_t materialFirst;
  uint32_t materialSize;
};

// abstract virtual class that is used by MaterialSet to deal with the textures ( check its derivative classes to see how it works )
class TextureRegistry
{
public:
  virtual void refreshStructures() = 0;
  virtual void validate(Device* device, CommandPool* commandPool, VkQueue queue) = 0;
  virtual void setTexture(uint32_t slotIndex, uint32_t layerIndex, const gli::texture& tex) = 0;
};

// MaterialSet is the class that stores information about asset materials in a single place both in CPU and in GPU.
// Material is a template T - you can use whatever structure you want, as long as the struct T :
//   - is std430 compatible ( because it will be sent to GPU )
//   - includes following methods :
//      void registerProperties(const pumex::Material& material)
//      void registerTextures(const std::map<pumex::TextureSemantic::Type, uint32_t>& textureIndices)
// Check out different MaterialData implementations in examples ( crowd, gpucull and deferred ).
template <typename T>
class MaterialSet
{
public:
  MaterialSet()                              = delete;
  explicit MaterialSet(std::shared_ptr<Viewer> viewer, std::shared_ptr<TextureRegistry> textureRegistry, std::weak_ptr<DeviceMemoryAllocator> allocator, const std::vector<TextureSemantic>& textureSemantic);
  MaterialSet(const MaterialSet&)            = delete;
  MaterialSet& operator=(const MaterialSet&) = delete;
  virtual ~MaterialSet();

  bool getTargetTextureNames(uint32_t index, std::vector<std::string>& texNames) const;
  bool setTargetTextureLayer(uint32_t index, uint32_t layer, const std::string& fileName, const gli::texture& tex);

  void registerMaterials(uint32_t typeID, std::shared_ptr<Asset> asset);
  std::map<TextureSemantic::Type, uint32_t> registerTextures(const Material& mat);

  std::vector<Material> getMaterials(uint32_t typeID) const;
  uint32_t getMaterialVariantCount(uint32_t typeID) const;
  void setMaterialVariant(uint32_t typeID, uint32_t materialVariant, const std::vector<Material>& materials);
  void refreshMaterialStructures();

  void validate(Device* device, CommandPool* commandPool, VkQueue queue);

  std::shared_ptr<StorageBuffer<MaterialTypeDefinition>>    typeDefinitionSbo;
  std::shared_ptr<StorageBuffer<MaterialVariantDefinition>> materialVariantSbo;
  std::shared_ptr<StorageBuffer<T>>                         materialDefinitionSbo;

private:

  struct InternalMaterialDefinition
  {
    InternalMaterialDefinition(uint32_t tid, uint32_t mv, uint32_t ai, uint32_t mi, const T& md )
      : typeID{ tid }, materialVariant{ mv }, assetIndex{ ai }, materialIndex{ mi }, materialDefinition( md )
    {
    }

    uint32_t typeID;
    uint32_t materialVariant;
    uint32_t assetIndex;
    uint32_t materialIndex;
    T        materialDefinition;
  };

  std::weak_ptr<Viewer>                        viewer;
  std::shared_ptr<TextureRegistry>             textureRegistry;
  std::weak_ptr<DeviceMemoryAllocator>         allocator;
  std::vector<TextureSemantic>                 semantics;
  std::map<uint32_t, std::vector<std::string>> textureNames;

  std::vector< InternalMaterialDefinition >    iMaterialDefinitions;
  std::vector<std::shared_ptr<Asset>>          assets; // material set owns assets

  std::vector<MaterialTypeDefinition>          typeDefinitions;
  std::vector<MaterialVariantDefinition>       variantDefinitions;
  std::vector<T>                               materialDefinitions;

};

class TextureRegistryTextureArray : public TextureRegistry
{
public:
  void setTargetTexture(uint32_t slotIndex, std::shared_ptr<Texture> texture)
  {
    textures[slotIndex] = texture;
  }
  std::shared_ptr<Texture> getTargetTexture(uint32_t slotIndex)
  {
    auto it = textures.find(slotIndex);
    if (it == textures.end())
      return std::shared_ptr<Texture>();
    return textures[slotIndex];
  }

  void refreshStructures() override
  {
  }

  void validate(Device* device, CommandPool* commandPool, VkQueue queue) override
  {
    for (auto t : textures)
      t.second->validate(device, commandPool, queue);
  }

  void setTexture(uint32_t slotIndex, uint32_t layerIndex, const gli::texture& tex)
  {
    auto it = textures.find(slotIndex);
    if (it == textures.end())
      return;
    it->second->setLayer(layerIndex, tex);
  }

  std::map<uint32_t, std::shared_ptr<Texture>> textures;
};

class TextureRegistryArrayOfTextures;

class ArrayOfTexturesDescriptorSetSource : public DescriptorSetSource
{
public:
  ArrayOfTexturesDescriptorSetSource(TextureRegistryArrayOfTextures* o);
  void getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const override;
private:
  TextureRegistryArrayOfTextures* owner;
};

class TextureRegistryArrayOfTextures : public TextureRegistry
{
public:
  TextureRegistryArrayOfTextures(std::weak_ptr<DeviceMemoryAllocator> allocator, std::weak_ptr<DeviceMemoryAllocator> textureAlloc)
    : textureAllocator{ textureAlloc }
  {
    textureSamplerOffsets = std::make_shared<StorageBuffer<uint32_t>>(allocator);
  }
  
  void setTargetTextureTraits(uint32_t slotIndex, const TextureTraits& textureTrait)
  {
    textureTraits[slotIndex] = textureTrait;
    textures[slotIndex]      = std::vector<std::shared_ptr<Texture>>();
  }

  std::shared_ptr<ArrayOfTexturesDescriptorSetSource> getTextureSamplerDescriptorSetSource()
  {
    if (textureSamplerDescriptorSetSource.get() == nullptr)
      textureSamplerDescriptorSetSource = std::make_shared<ArrayOfTexturesDescriptorSetSource>(this);
    return textureSamplerDescriptorSetSource;
  }

  void refreshStructures() override
  {
    std::vector<uint32_t> tso(TextureSemantic::Type::TextureSemanticCount);
    std::fill(tso.begin(), tso.end(), 0);
    uint32_t textureSum = 0;
    for (uint32_t i = 0; i < TextureSemantic::Type::TextureSemanticCount; ++i)
    {
      auto it = textures.find(i);
      if (it == textures.end())
        continue;

      tso[i]     = textureSum;
      textureSum += textures[i].size();
    }
    textureSamplersQuantity = textureSum;
    textureSamplerOffsets->set(tso);
  }

  void validate(Device* device, CommandPool* commandPool, VkQueue queue) override
  {
    for (uint32_t i = 0; i < TextureSemantic::Type::TextureSemanticCount; ++i)
    {
      auto it = textures.find(i);
      if (it == textures.end())
        continue;
      for (auto tx : it->second)
        tx->validate(device, commandPool, queue);
    }
    textureSamplerOffsets->validate(device, commandPool, queue);

    if (textureSamplerDescriptorSetSource.get() != nullptr)
      textureSamplerDescriptorSetSource->notifyDescriptorSets();
  }

  void setTexture(uint32_t slotIndex, uint32_t layerIndex, const gli::texture& tex)
  {
    auto it = textures.find(slotIndex);
    if (it == textures.end())
      return;// FIXME : CHECK_LOG_THROW ?
    if (layerIndex >= it->second.size())
      it->second.resize(layerIndex+1);
    it->second[layerIndex] = std::make_shared<Texture>(tex, textureTraits[slotIndex], textureAllocator);
  }

  std::shared_ptr<StorageBuffer<uint32_t>>                         textureSamplerOffsets;
  friend class ArrayOfTexturesDescriptorSetSource;
protected:
  std::weak_ptr<DeviceMemoryAllocator>                      textureAllocator;
  std::map<uint32_t, std::vector<std::shared_ptr<Texture>>> textures;
  std::map<uint32_t, TextureTraits>                         textureTraits;
  uint32_t                                                  textureSamplersQuantity = 0;
  std::shared_ptr<ArrayOfTexturesDescriptorSetSource>                 textureSamplerDescriptorSetSource;

};

ArrayOfTexturesDescriptorSetSource::ArrayOfTexturesDescriptorSetSource(TextureRegistryArrayOfTextures* o)
  : owner{ o }
{
}
void ArrayOfTexturesDescriptorSetSource::getDescriptorSetValues(VkDevice device, uint32_t index, std::vector<DescriptorSetValue>& values) const
{
  CHECK_LOG_THROW(owner == nullptr, "ArrayOfTexturesDescriptorSetSource::getDescriptorSetValue() : owner not defined");
  values.reserve(values.size() + owner->textureSamplersQuantity);
  for (uint32_t i = 0; i < TextureSemantic::Type::TextureSemanticCount; ++i)
  {
    auto it = owner->textures.find(i);
    if (it == owner->textures.end())
      continue;
    for (auto tx : it->second)
      tx->getDescriptorSetValues(device, index, values);
  }
}

class TextureRegistryNull : public TextureRegistry
{
public:
  void refreshStructures() override
  {
  }
  void validate(Device* device, CommandPool* commandPool, VkQueue queue) override
  {
  }
  void setTexture(uint32_t slotIndex, uint32_t layerIndex, const gli::texture& tex)
  {
  }
};


template <typename T>
MaterialSet<T>::MaterialSet(std::shared_ptr<Viewer> v, std::shared_ptr<TextureRegistry> tr, std::weak_ptr<DeviceMemoryAllocator> a, const std::vector<TextureSemantic>& ts)
  : viewer{ v }, textureRegistry{ tr }, allocator{ a }, semantics(ts)
{
  typeDefinitionSbo     = std::make_shared<StorageBuffer<MaterialTypeDefinition>>(a);
  materialVariantSbo    = std::make_shared<StorageBuffer<MaterialVariantDefinition>>(a);
  materialDefinitionSbo = std::make_shared<StorageBuffer<T>>(a);

  for (const auto& s : semantics)
    textureNames[s.index] = std::vector<std::string>();
}


template <typename T>
MaterialSet<T>::~MaterialSet()
{
//  for (auto& pdd : perDeviceData)
//      pdd.second.deleteBuffers(pdd.first);
}

template <typename T>
bool MaterialSet<T>::getTargetTextureNames(uint32_t index, std::vector<std::string>& texNames) const
{
  auto it = textureNames.find(index);
  if (it == textureNames.end())
    return false;
  texNames = it->second;
}

template <typename T>
bool MaterialSet<T>::setTargetTextureLayer(uint32_t slotIndex, uint32_t layerIndex, const std::string& fileName, const gli::texture& tex)
{
  auto nit = textureNames.find(slotIndex);
  if (nit == textureNames.end())
    return false;
  if (nit->second.size() <= layerIndex)
    nit->second.resize(layerIndex+1);
  nit->second[layerIndex] = fileName;
  textureRegistry->setTexture(slotIndex,layerIndex,tex);
  return true;
}


template <typename T>
void MaterialSet<T>::registerMaterials(uint32_t typeID, std::shared_ptr<Asset> asset)
{
  // register asset
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

  // register materials as default material variant (=0)
  for (uint32_t m = 0; m < asset->materials.size(); ++m)
  {
    std::map<TextureSemantic::Type, uint32_t> registeredTextures = registerTextures(asset->materials[m]);
    T material;
    material.registerTextures(registeredTextures);
    material.registerProperties(asset->materials[m]);
    iMaterialDefinitions.push_back(InternalMaterialDefinition(typeID, 0, assetIndex, m, material ));
  }
}

template <typename T>
std::map<TextureSemantic::Type, uint32_t> MaterialSet<T>::registerTextures(const Material& mat)
{
  // register all found textures for a given material
  std::map<TextureSemantic::Type, uint32_t> registeredTextures;
  for (auto it = mat.textures.cbegin(), eit = mat.textures.end(); it != eit; ++it)
  {
    for (const TextureSemantic& s : semantics)
    {
      if (it->first == s.type)
      {
        // according to semantics defined for this render mask - we have to add a texture to a target texture number s.index
        // but first - we must check if this texture has not been already added
        uint32_t textureIndex = UINT32_MAX;
        for (unsigned int i = 0; i<textureNames[s.index].size(); ++i)
        {
          if (textureNames[s.index][i] == it->second)
          {
            textureIndex = i;
            break;
          }
        }
        if (textureIndex == UINT32_MAX)
        {
          textureIndex = textureNames[s.index].size();
          textureNames[s.index].push_back(it->second);

          std::string fullFileName = viewer.lock()->getFullFilePath(it->second);
          CHECK_LOG_THROW(fullFileName.empty(), "Cannot find file : " << it->second);
          gli::texture tex(gli::load(fullFileName));
          CHECK_LOG_THROW(tex.empty(), "Texture not loaded : " << it->second);
          textureRegistry->setTexture(s.index, textureIndex, tex);
        }
        registeredTextures[s.type] = textureIndex;
      }
    }
  }
  return registeredTextures;
}

template <typename T>
std::vector<Material> MaterialSet<T>::getMaterials(uint32_t typeID) const
{
  std::vector<Material> materials;
  for (auto it = iMaterialDefinitions.begin(), eit = iMaterialDefinitions.end(); it != eit; ++it)
  {
    if (it->typeID == typeID && it->materialVariant==0)
      materials.push_back(assets[it->assetIndex]->materials[it->materialIndex]);
  }
  return materials;
}

template <typename T>
uint32_t MaterialSet<T>::getMaterialVariantCount(uint32_t typeID) const
{
  std::set<uint32_t> matVars;
  for (auto it = iMaterialDefinitions.begin(), eit = iMaterialDefinitions.end(); it != eit; ++it)
  {
    if (it->typeID == typeID)
      matVars.insert( it->materialVariant);
  }
  return matVars.size();
}

template <typename T>
void MaterialSet<T>::setMaterialVariant(uint32_t typeID, uint32_t materialVariant, const std::vector<Material>& materials)
{
  for (uint32_t m = 0; m < materials.size(); ++m)
  {
    std::map<TextureSemantic::Type, uint32_t> registeredTextures = registerTextures(materials[m]);
    T material;
    material.registerTextures(registeredTextures);
    material.registerProperties(materials[m]);
    // because we don't know which asset are materials from - we set asset index and material index to 0
    // Only materialVariant 0 has proper assetIndex and materialIndex values set
    iMaterialDefinitions.push_back(InternalMaterialDefinition(typeID, materialVariant, 0, 0, material));
  }
}


template <typename T>
void MaterialSet<T>::refreshMaterialStructures()
{
  // find the number of types
  uint32_t typeCount = 0;
  if (iMaterialDefinitions.size() > 0)
    typeCount = std::max_element(iMaterialDefinitions.begin(), iMaterialDefinitions.end(), [](const InternalMaterialDefinition& lhs, const InternalMaterialDefinition& rhs){return lhs.typeID < rhs.typeID; })->typeID + 1;
  typeDefinitions.resize(typeCount);
  variantDefinitions.resize(0);
  materialDefinitions.resize(0);

  std::sort(iMaterialDefinitions.begin(), iMaterialDefinitions.end(), [](const InternalMaterialDefinition& lhs, const InternalMaterialDefinition& rhs){ if (lhs.typeID != rhs.typeID) return lhs.typeID < rhs.typeID; if (lhs.materialVariant != rhs.materialVariant) return lhs.materialVariant < rhs.materialVariant; if (lhs.assetIndex != rhs.assetIndex) return lhs.assetIndex < rhs.assetIndex; return lhs.materialIndex < rhs.materialIndex; });
  for (uint32_t t = 0; t < typeDefinitions.size(); ++t)
  {
    typeDefinitions[t].variantFirst = variantDefinitions.size();
    auto typePair = std::equal_range(iMaterialDefinitions.begin(), iMaterialDefinitions.end(), InternalMaterialDefinition(t, 0, 0, 0, T()), [](const InternalMaterialDefinition& lhs, const InternalMaterialDefinition& rhs){return lhs.typeID < rhs.typeID; });
    uint32_t v = 0;
    while (true)
    {
      auto variantPair = std::equal_range(typePair.first, typePair.second, InternalMaterialDefinition(t, v, 0, 0, T()), [](const InternalMaterialDefinition& lhs, const InternalMaterialDefinition& rhs){return lhs.materialVariant < rhs.materialVariant; });
      // if variant is empty - it means that there is no more variants left for that type
      if (variantPair.first == variantPair.second)
        break;
      
      MaterialVariantDefinition varDef;
      varDef.materialFirst = materialDefinitions.size();
      for (auto it = variantPair.first; it!= variantPair.second; ++it)
      {
        // for the first variant ( created directly from models ) we need to register our materials in all assets...
        if (v == 0)
        {
          float currentMaterialIndex = (float)(materialDefinitions.size() - varDef.materialFirst);
          for (Geometry& geom : assets[it->assetIndex]->geometries)
          {
            if (geom.materialIndex == it->materialIndex)
            {
              // find first tex coord of size bigger than 2
              uint32_t offset = 0;
              bool found = false;
              for (VertexSemantic s : geom.semantic)
              {
                if (s.type == VertexSemantic::TexCoord && s.size > 2)
                {
                  offset+=2;
                  found = true;
                  break;
                }
                offset+=s.size;
              }
              if (!found)
              {
                LOG_ERROR << "Found geometry without Texcoord with size > 2"<<std::endl;
                continue;
              }
              uint32_t vertexSize = calcVertexSize(geom.semantic);
              for (size_t i = offset; i < geom.vertices.size(); i += vertexSize )
                geom.vertices[i] = currentMaterialIndex;
            }
          }
        }
        materialDefinitions.push_back(it->materialDefinition);
      }

      varDef.materialSize = materialDefinitions.size() - varDef.materialFirst;
      variantDefinitions.push_back(varDef);
      v++;
    }
    typeDefinitions[t].variantSize = variantDefinitions.size() - typeDefinitions[t].variantFirst;
  }
  typeDefinitionSbo->set(typeDefinitions);
  materialVariantSbo->set(variantDefinitions);
  materialDefinitionSbo->set(materialDefinitions);

  textureRegistry->refreshStructures();
}

template <typename T>
void MaterialSet<T>::validate(Device* device, CommandPool* commandPool, VkQueue queue)
{
  typeDefinitionSbo->validate(device, commandPool, queue);
  materialVariantSbo->validate(device, commandPool, queue);
  materialDefinitionSbo->validate(device, commandPool, queue);
  textureRegistry->validate(device, commandPool, queue);
}

}