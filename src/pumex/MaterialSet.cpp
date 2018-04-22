//
// Copyright(c) 2017-2018 Paweł Księżopolski ( pumexx )
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

#include <pumex/MaterialSet.h>
#include <pumex/Asset.h>
#include <pumex/Sampler.h>
#include <pumex/Texture.h>
#include <pumex/Command.h>
#include <pumex/Viewer.h>

using namespace pumex;

TextureRegistryBase::~TextureRegistryBase()
{
}

MaterialRegistryBase::~MaterialRegistryBase()
{
}

MaterialSet::MaterialSet(std::shared_ptr<Viewer> v, std::shared_ptr<MaterialRegistryBase> mr, std::shared_ptr<TextureRegistryBase> tr, std::shared_ptr<DeviceMemoryAllocator> a, const std::vector<TextureSemantic>& ts)
  : viewer{ v }, materialRegistry{ mr }, textureRegistry { tr }, allocator{ a }, semantics(ts)
{
  typeDefinitionSbo = std::make_shared<StorageBuffer<MaterialTypeDefinition>>(a);
  materialVariantSbo = std::make_shared<StorageBuffer<MaterialVariantDefinition>>(a);

  for (const auto& s : semantics)
    textureNames[s.index] = std::vector<std::string>();
}

MaterialSet::~MaterialSet()
{
  //  for (auto& pdd : perDeviceData)
  //      pdd.second.deleteBuffers(pdd.first);
}

void MaterialSet::validate(const RenderContext& renderContext)
{
  // FIXME : missing material set validation
}

bool MaterialSet::getTargetTextureNames(uint32_t index, std::vector<std::string>& texNames) const
{
  auto it = textureNames.find(index);
  if (it == end(textureNames))
    return false;
  texNames = it->second;
  return true;
}

bool MaterialSet::setTargetTextureLayer(uint32_t slotIndex, uint32_t layerIndex, const std::string& fileName, std::shared_ptr<gli::texture> tex)
{
  auto nit = textureNames.find(slotIndex);
  if (nit == end(textureNames))
    return false;
  if (nit->second.size() <= layerIndex)
    nit->second.resize(layerIndex + 1);
  nit->second[layerIndex] = fileName;
  textureRegistry->setTexture(slotIndex, layerIndex, tex);
  return true;
}

void MaterialSet::registerMaterials(uint32_t typeID, std::shared_ptr<Asset> asset)
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
    materialRegistry->registerMaterial(typeID, 0, assetIndex, m, asset->materials[m], registeredTextures);
  }
}

void MaterialSet::setMaterialVariant(uint32_t typeID, uint32_t materialVariant, const std::vector<Material>& materials)
{
  for (uint32_t m = 0; m < materials.size(); ++m)
  {
    std::map<TextureSemantic::Type, uint32_t> registeredTextures = registerTextures(materials[m]);
    materialRegistry->registerMaterial(typeID, materialVariant, 0, 0, materials[m], registeredTextures);
  }
}

std::vector<Material> MaterialSet::getMaterials(uint32_t typeID) const
{
  std::vector<Material> materials;
  auto materialIndices = materialRegistry->getAssetMaterialIndices(typeID);
  for (const auto& m : materialIndices)
    materials.push_back(assets[m.first]->materials[m.second]);
  return materials;
}

uint32_t MaterialSet::getMaterialVariantCount(uint32_t typeID) const
{
  return materialRegistry->getMaterialVariantCount(typeID);
}

void MaterialSet::refreshMaterialStructures()
{
  materialRegistry->buildTypesAndVariants(typeDefinitions, variantDefinitions);

  for (uint32_t t = 0; t < typeDefinitions.size(); ++t)
  {
    auto assetMaterialIndices = materialRegistry->getAssetMaterialIndices(t);
    for (uint32_t i = 0; i < assetMaterialIndices.size(); ++i)
    {
      uint32_t assetIndex = assetMaterialIndices[i].first;
      uint32_t materialIndex = assetMaterialIndices[i].second;

      for (Geometry& geom : assets[assetIndex]->geometries)
      {
        if (geom.materialIndex == materialIndex)
        {
          // find first tex coord of size bigger than 2
          uint32_t offset = 0;
          bool found = false;
          for (VertexSemantic s : geom.semantic)
          {
            if (s.type == VertexSemantic::TexCoord && s.size > 2)
            {
              offset += 2;
              found = true;
              break;
            }
            offset += s.size;
          }
          if (!found)
          {
            LOG_ERROR << "Found geometry without Texcoord with size > 2" << std::endl;
            continue;
          }
          uint32_t vertexSize = calcVertexSize(geom.semantic);
          for (size_t j = offset; j < geom.vertices.size(); j += vertexSize)
            geom.vertices[j] = i;
        }
      }
    }
  }

  typeDefinitionSbo->set(typeDefinitions);
  materialVariantSbo->set(variantDefinitions);
  textureRegistry->refreshStructures();
}

std::map<TextureSemantic::Type, uint32_t> MaterialSet::registerTextures(const Material& mat)
{
  // register all found textures for a given material
  std::map<TextureSemantic::Type, uint32_t> registeredTextures;
  for (auto it = cbegin(mat.textures), eit = cend(mat.textures); it != eit; ++it)
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
          auto tex = std::make_shared<gli::texture>(gli::load(fullFileName));
          CHECK_LOG_THROW(tex->empty(), "Texture not loaded : " << it->second);
          textureRegistry->setTexture(s.index, textureIndex, tex);
        }
        registeredTextures[s.type] = textureIndex;
      }
    }
  }
  return registeredTextures;
}

void TextureRegistryTextureArray::setTargetTexture(uint32_t slotIndex, std::shared_ptr<Texture> texture)
{
  textures[slotIndex] = texture;
}

std::shared_ptr<Texture> TextureRegistryTextureArray::getTargetTexture(uint32_t slotIndex)
{
  auto it = textures.find(slotIndex);
  if (it == end(textures))
    return std::shared_ptr<Texture>();
  return textures[slotIndex];
}

void TextureRegistryTextureArray::refreshStructures()
{
}

void TextureRegistryTextureArray::setTexture(uint32_t slotIndex, uint32_t layerIndex, std::shared_ptr<gli::texture> tex)
{
  auto it = textures.find(slotIndex);
  if (it == end(textures))
    return;
  it->second->setLayer(layerIndex, tex);
}

TextureRegistryArrayOfTextures::TextureRegistryArrayOfTextures(std::shared_ptr<DeviceMemoryAllocator> allocator, std::shared_ptr<DeviceMemoryAllocator> textureAlloc)
  : textureAllocator{ textureAlloc }
{
}

void TextureRegistryArrayOfTextures::setTextureSampler(uint32_t slotIndex, std::shared_ptr<Sampler> sampler)
{
  textureSamplers[slotIndex] = sampler;
  textures[slotIndex]        = std::vector<std::shared_ptr<Resource>>();
}

std::vector<std::shared_ptr<Resource>> TextureRegistryArrayOfTextures::getTextures(uint32_t slotIndex)
{
  auto it = textures.find(slotIndex);
  if (it == end(textures))
    return std::vector<std::shared_ptr<Resource>>();
  return it->second;
}

void TextureRegistryArrayOfTextures::refreshStructures()
{
}

void TextureRegistryArrayOfTextures::setTexture(uint32_t slotIndex, uint32_t layerIndex, std::shared_ptr<gli::texture> tex)
{
  auto it = textures.find(slotIndex);
  if (it == end(textures))
    return;// FIXME : CHECK_LOG_THROW ?
  if (layerIndex >= it->second.size())
    it->second.resize(layerIndex + 1);
  // this texture will not be modified by GPU, so it is enough to declare it as OnceForAllSwapChainImages
  it->second[layerIndex] = std::make_shared<Texture>(tex, textureSamplers[slotIndex], textureAllocator, VK_IMAGE_USAGE_SAMPLED_BIT, Resource::OnceForAllSwapChainImages);
}

