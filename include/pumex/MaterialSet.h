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
#include <map>
#include <set>
#include <vulkan/vulkan.h>
#include <gli/load.hpp>
#include <pumex/Export.h>
#include <pumex/MemoryBuffer.h>

namespace pumex
{

class Asset;
struct Material;
class Resource;
class Sampler;
class MemoryImage;
class CombinedImageSampler;
class DeviceMemoryAllocator;
class Viewer;
class RenderContext;
template <typename T> class Buffer;

// Assimp does not load textures, but only its names and semantics ( diffuse, normal, etc )
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
class PUMEX_EXPORT TextureRegistryBase
{
public:
  virtual ~TextureRegistryBase();

  virtual void setTexture(uint32_t slotIndex, uint32_t layerIndex, std::shared_ptr<gli::texture> tex) = 0;
};

// abstract virtual class that is used to deal with the materials
class PUMEX_EXPORT MaterialRegistryBase
{
public:
  virtual ~MaterialRegistryBase();

  virtual void                  registerMaterial(uint32_t typeID, uint32_t materialVariant, uint32_t materialIndex, const Material& mat, const std::map<TextureSemantic::Type, uint32_t>& registeredTextures) = 0;
  virtual std::vector<Material> getMaterials(uint32_t typeID) const = 0;
  virtual uint32_t              getMaterialVariantCount(uint32_t typeID) const = 0;
  virtual void                  buildTypesAndVariants(std::vector<MaterialTypeDefinition>& typeDefinitions, std::vector<MaterialVariantDefinition>& variantDefinitions) = 0;
};

// MaterialSet is the class that stores information about asset materials in a single place both in CPU and in GPU.
// Material is a template T - you can use whatever structure you want, as long as the struct T :
//   - is std430 compatible ( because it will be sent to GPU )
//   - includes following methods :
//      void registerProperties(const Material& material)
//      void registerTextures(const std::map<TextureSemantic::Type, uint32_t>& textureIndices)
// Check out different MaterialData implementations in examples ( crowd, gpucull and deferred ).
class PUMEX_EXPORT MaterialSet
{
public:
  MaterialSet()                              = delete;
  explicit MaterialSet(std::shared_ptr<Viewer> viewer, std::shared_ptr<MaterialRegistryBase> materialRegistry, std::shared_ptr<TextureRegistryBase> textureRegistry, std::shared_ptr<DeviceMemoryAllocator> allocator, const std::vector<TextureSemantic>& textureSemantic);
  MaterialSet(const MaterialSet&)            = delete;
  MaterialSet& operator=(const MaterialSet&) = delete;
  virtual ~MaterialSet();

  bool                                         getTargetTextureNames(uint32_t index, std::vector<std::string>& texNames) const;
  bool                                         setTargetTextureLayer(uint32_t index, uint32_t layer, const std::string& fileName, std::shared_ptr<gli::texture> tex);

  void                                         registerMaterials(uint32_t typeID, std::shared_ptr<Asset> asset);
  void                                         registerMaterialVariant(uint32_t typeID, uint32_t materialVariant, const std::vector<Material>& materials);
  void                                         endRegisterMaterials();

  std::vector<Material>                        getMaterials(uint32_t typeID) const;
  uint32_t                                     getMaterialVariantCount(uint32_t typeID) const;

  std::shared_ptr<Buffer<std::vector<MaterialTypeDefinition>>>    typeDefinitionBuffer;
  std::shared_ptr<Buffer<std::vector<MaterialVariantDefinition>>> materialVariantBuffer;

private:

  std::map<TextureSemantic::Type, uint32_t>    registerTextures(const Material& mat);

  std::weak_ptr<Viewer>                        viewer;
  std::shared_ptr<MaterialRegistryBase>        materialRegistry;
  std::shared_ptr<TextureRegistryBase>         textureRegistry;
  std::vector<TextureSemantic>                 semantics;
  std::map<uint32_t, std::vector<std::string>> textureNames;
};


// material registry that is able to store any material in a form of T class
template <typename T>
class MaterialRegistry : public MaterialRegistryBase
{
public:
  MaterialRegistry(std::shared_ptr<DeviceMemoryAllocator> allocator);

  std::shared_ptr<std::vector<T>>            materialDefinitions;
  std::shared_ptr<Buffer<std::vector<T>>>    materialDefinitionBuffer;

  void                                       registerMaterial(uint32_t typeID, uint32_t materialVariant, uint32_t materialIndex, const Material& mat, const std::map<TextureSemantic::Type, uint32_t>& registeredTextures) override;
  std::vector<Material>                      getMaterials(uint32_t typeID) const override;
  uint32_t                                   getMaterialVariantCount(uint32_t typeID) const override;
  void                                       buildTypesAndVariants(std::vector<MaterialTypeDefinition>& typeDefinitions, std::vector<MaterialVariantDefinition>& variantDefinitions) override;
protected:
  struct InternalMaterialDefinition
  {
    InternalMaterialDefinition(uint32_t tid, uint32_t mv, uint32_t mi, const T& md)
      : typeID{ tid }, materialVariant{ mv }, materialIndex{ mi }, materialDefinition{ md }
    {
    }

    uint32_t    typeID;
    uint32_t    materialVariant;
    uint32_t    materialIndex;
    T           materialDefinition;
  };
  std::vector< InternalMaterialDefinition > iMaterialDefinitions;
  std::map<uint32_t, std::vector<Material>> materials;
};


class PUMEX_EXPORT TextureRegistryTextureArray : public TextureRegistryBase
{
public:
  void                                          setTargetTexture(uint32_t slotIndex, std::shared_ptr<MemoryImage> texture, std::shared_ptr<Sampler> sampler);
  std::shared_ptr<Resource>                     getCombinedImageSampler(uint32_t slotIndex);

  void                                          setTexture(uint32_t slotIndex, uint32_t layerIndex, std::shared_ptr<gli::texture> tex) override;

  std::map<uint32_t, std::shared_ptr<MemoryImage>>          memoryImages;
  std::map<uint32_t, std::shared_ptr<CombinedImageSampler>> resources;
};

class PUMEX_EXPORT TextureRegistryArrayOfTextures : public TextureRegistryBase
{
public:
  TextureRegistryArrayOfTextures(std::shared_ptr<DeviceMemoryAllocator> allocator, std::shared_ptr<DeviceMemoryAllocator> textureAlloc);
  
  void                                                       setTextureSampler(uint32_t slotIndex, std::shared_ptr<Sampler> sampler);
  std::vector<std::shared_ptr<Resource>>&                    getCombinedImageSamplers(uint32_t slotIndex);

  void                                                       setTexture(uint32_t slotIndex, uint32_t layerIndex, std::shared_ptr<gli::texture> tex) override;

protected:
  std::shared_ptr<DeviceMemoryAllocator>                         textureAllocator;
  std::map<uint32_t, std::vector<std::shared_ptr<MemoryImage>>>  memoryImages;
  std::map<uint32_t, std::shared_ptr<Sampler>>                   textureSamplers;
  std::map<uint32_t, std::vector<std::shared_ptr<Resource>>>     resources;
};


class TextureRegistryNull : public TextureRegistryBase
{
public:
  void setTexture(uint32_t slotIndex, uint32_t layerIndex, std::shared_ptr<gli::texture> tex) override
  {
  }
};

template <typename T>
MaterialRegistry<T>::MaterialRegistry(std::shared_ptr<DeviceMemoryAllocator> allocator)
{
  materialDefinitions      = std::make_shared<std::vector<T>>();
  materialDefinitionBuffer = std::make_shared<Buffer<std::vector<T>>>(materialDefinitions, allocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pbPerDevice, swForEachImage);
}

template <typename T>
void MaterialRegistry<T>::registerMaterial(uint32_t typeID, uint32_t materialVariant, uint32_t materialIndex, const Material& mat, const std::map<TextureSemantic::Type, uint32_t>& registeredTextures)
{
  if (materialVariant == 0)
  {
    auto it = materials.find(typeID);
    if (it == end(materials))
      it = materials.insert({ typeID, std::vector<Material>() }).first;
    it->second.push_back(mat);
  }

  T material;
  material.registerTextures(registeredTextures);
  material.registerProperties(mat);
  iMaterialDefinitions.push_back(InternalMaterialDefinition(typeID, materialVariant, materialIndex, material));
  std::sort(begin(iMaterialDefinitions), end(iMaterialDefinitions), [](const InternalMaterialDefinition& lhs, const InternalMaterialDefinition& rhs) { if (lhs.typeID != rhs.typeID) return lhs.typeID < rhs.typeID; if (lhs.materialVariant != rhs.materialVariant) return lhs.materialVariant < rhs.materialVariant; return lhs.materialIndex < rhs.materialIndex; });
}

template <typename T>
std::vector<Material> MaterialRegistry<T>::getMaterials(uint32_t typeID) const
{
  auto it = materials.find(typeID);
  if (it == end(materials))
    return std::vector<Material>();
  return it->second;
}

template <typename T>
uint32_t MaterialRegistry<T>::getMaterialVariantCount(uint32_t typeID) const
{
  std::set<uint32_t> matVars;
  for (auto it = begin(iMaterialDefinitions), eit = end(iMaterialDefinitions); it != eit; ++it)
  {
    if (it->typeID == typeID)
      matVars.insert(it->materialVariant);
  }
  return matVars.size();
}

template <typename T>
void MaterialRegistry<T>::buildTypesAndVariants(std::vector<MaterialTypeDefinition>& typeDefinitions, std::vector<MaterialVariantDefinition>& variantDefinitions)
{
  // find the number of types
  uint32_t typeCount = 0;
  if (iMaterialDefinitions.size() > 0)
    typeCount = std::max_element(begin(iMaterialDefinitions), end(iMaterialDefinitions), [](const InternalMaterialDefinition& lhs, const InternalMaterialDefinition& rhs) {return lhs.typeID < rhs.typeID; })->typeID + 1;

  typeDefinitions.resize(typeCount);
  variantDefinitions.resize(0);
  materialDefinitions->resize(0);

  for (uint32_t typeIndex = 0; typeIndex < typeDefinitions.size(); ++typeIndex)
  {
    typeDefinitions[typeIndex].variantFirst = variantDefinitions.size();
    auto typePair = std::equal_range(begin(iMaterialDefinitions), end(iMaterialDefinitions), InternalMaterialDefinition(typeIndex, 0, 0, T()), [](const InternalMaterialDefinition& lhs, const InternalMaterialDefinition& rhs) {return lhs.typeID < rhs.typeID; });

    uint32_t variantIndex = 0;
    while (true)
    {
      auto variantPair = std::equal_range(typePair.first, typePair.second, InternalMaterialDefinition(typeIndex, variantIndex, 0, T()), [](const InternalMaterialDefinition& lhs, const InternalMaterialDefinition& rhs) {return lhs.materialVariant < rhs.materialVariant; });
      // if variant is empty - it means that there is no more variants left for that type
      if (variantPair.first == variantPair.second)
        break;

      MaterialVariantDefinition varDef;
      varDef.materialFirst = materialDefinitions->size();
      for (auto it = variantPair.first; it != variantPair.second; ++it)
        materialDefinitions->push_back(it->materialDefinition);

      varDef.materialSize = materialDefinitions->size() - varDef.materialFirst;
      variantDefinitions.push_back(varDef);
      variantIndex++;
    }
    typeDefinitions[typeIndex].variantSize = variantDefinitions.size() - typeDefinitions[typeIndex].variantFirst;
  }
  materialDefinitionBuffer->invalidateData();
}


}