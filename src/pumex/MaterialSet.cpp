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
#include <pumex/MemoryImage.h>
#include <pumex/Command.h>
#include <pumex/Viewer.h>
#include <pumex/CombinedImageSampler.h>
#include <pumex/SampledImage.h>
#include <pumex/StorageImage.h>

using namespace pumex;

TextureRegistryBase::~TextureRegistryBase()
{
}

MaterialRegistryBase::~MaterialRegistryBase()
{
}

MaterialSet::MaterialSet(std::shared_ptr<Viewer> v, std::shared_ptr<MaterialRegistryBase> mr, std::shared_ptr<TextureRegistryBase> tr, std::shared_ptr<DeviceMemoryAllocator> a, const std::vector<TextureSemantic>& ts)
  : viewer{ v }, materialRegistry{ mr }, textureRegistry { tr }, semantics(ts)
{
  typeDefinitionBuffer = std::make_shared<Buffer<std::vector<MaterialTypeDefinition>>>(std::make_shared<std::vector<MaterialTypeDefinition>>(), a, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pbPerDevice, swForEachImage);
  materialVariantBuffer = std::make_shared<Buffer<std::vector<MaterialVariantDefinition>>>(std::make_shared<std::vector<MaterialVariantDefinition>>(), a, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pbPerDevice, swForEachImage);

  for (const auto& s : semantics)
    textureNames[s.index] = std::vector<std::string>();
}

MaterialSet::~MaterialSet()
{
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
  // register materials as default material variant ( = 0 )
  uint32_t materialOffset = materialRegistry->getMaterials(typeID).size();
  for (uint32_t m = 0; m < asset->materials.size(); ++m)
  {
    std::map<TextureSemantic::Type, uint32_t> registeredTextures = registerTextures(asset->materials[m]);
    materialRegistry->registerMaterial(typeID, 0, materialOffset+m, asset->materials[m], registeredTextures);
  }

  for (Geometry& geom : asset->geometries)
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
      geom.vertices[j] = materialOffset + geom.materialIndex;
  }
}

void MaterialSet::registerMaterialVariant(uint32_t typeID, uint32_t materialVariant, const std::vector<Material>& materials)
{
  CHECK_LOG_THROW(materialVariant == 0, "Cannot register material variant with index == 0");
  for (uint32_t m = 0; m < materials.size(); ++m)
  {
    std::map<TextureSemantic::Type, uint32_t> registeredTextures = registerTextures(materials[m]);
    materialRegistry->registerMaterial(typeID, materialVariant, m, materials[m], registeredTextures);
  }
}

void MaterialSet::endRegisterMaterials()
{
  materialRegistry->buildTypesAndVariants(*typeDefinitionBuffer->getData(), *materialVariantBuffer->getData());
  typeDefinitionBuffer->invalidateData();
  materialVariantBuffer->invalidateData();
}

std::vector<Material> MaterialSet::getMaterials(uint32_t typeID) const
{
  return materialRegistry->getMaterials(typeID);
}

uint32_t MaterialSet::getMaterialVariantCount(uint32_t typeID) const
{
  return materialRegistry->getMaterialVariantCount(typeID);
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
        uint32_t textureIndex = std::numeric_limits<uint32_t>::max();
        for (unsigned int i = 0; i<textureNames[s.index].size(); ++i)
        {
          if (textureNames[s.index][i] == it->second)
          {
            textureIndex = i;
            break;
          }
        }
        if (textureIndex == std::numeric_limits<uint32_t>::max())
        {
          textureIndex = textureNames[s.index].size();
          textureNames[s.index].push_back(it->second);

          auto tex = viewer.lock()->loadTexture(it->second, true);
          CHECK_LOG_THROW(tex->empty(), "Texture not loaded : " << it->second);
          textureRegistry->setTexture(s.index, textureIndex, tex);
        }
        registeredTextures[s.type] = textureIndex;
      }
    }
  }
  return registeredTextures;
}

void TextureRegistryTextureArray::setCombinedImageSampler(uint32_t slotIndex, std::shared_ptr<MemoryImage> memoryImage, std::shared_ptr<Sampler> sampler)
{
  memoryImages[slotIndex] = memoryImage;
  auto imageView = std::make_shared<ImageView>(memoryImage, memoryImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D_ARRAY);
  resources[slotIndex] = std::make_shared<CombinedImageSampler>(imageView, sampler);
}

void TextureRegistryTextureArray::setSampledImage(uint32_t slotIndex, std::shared_ptr<MemoryImage> memoryImage)
{
  memoryImages[slotIndex] = memoryImage;
  auto imageView = std::make_shared<ImageView>(memoryImage, memoryImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D_ARRAY);
  resources[slotIndex] = std::make_shared<SampledImage>(imageView);
}

void TextureRegistryTextureArray::setStorageImage(uint32_t slotIndex, std::shared_ptr<MemoryImage> memoryImage)
{
  memoryImages[slotIndex] = memoryImage;
  auto imageView = std::make_shared<ImageView>(memoryImage, memoryImage->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D_ARRAY);
  resources[slotIndex] = std::make_shared<StorageImage>(imageView);
}

std::shared_ptr<Resource> TextureRegistryTextureArray::getResource(uint32_t slotIndex)
{
  auto it = resources.find(slotIndex);
  CHECK_LOG_THROW(it == end(resources), "There's no resource registered. Slot index " << slotIndex);
  return it->second;
}

void TextureRegistryTextureArray::setTexture(uint32_t slotIndex, uint32_t layerIndex, std::shared_ptr<gli::texture> tex)
{
  auto it = memoryImages.find(slotIndex);
  if (it == end(memoryImages))
    return;
  it->second->setImageLayer(layerIndex, tex);
}

TextureRegistryArrayOfTextures::TextureRegistryArrayOfTextures(std::shared_ptr<DeviceMemoryAllocator> allocator, std::shared_ptr<DeviceMemoryAllocator> textureAlloc)
  : textureAllocator{ textureAlloc }
{
}

void TextureRegistryArrayOfTextures::setCombinedImageSampler(uint32_t slotIndex, std::shared_ptr<Sampler> sampler)
{
  textureTypes[slotIndex]    = 0;
  textureSamplers[slotIndex] = sampler;
  memoryImages[slotIndex]    = std::vector<std::shared_ptr<MemoryImage>>();
  resources[slotIndex]       = std::vector<std::shared_ptr<Resource>>();
}

void TextureRegistryArrayOfTextures::setSampledImage(uint32_t slotIndex)
{
  textureTypes[slotIndex]    = 1;
  memoryImages[slotIndex]    = std::vector<std::shared_ptr<MemoryImage>>();
  resources[slotIndex]       = std::vector<std::shared_ptr<Resource>>();
}

void TextureRegistryArrayOfTextures::setStorageImage(uint32_t slotIndex)
{
  textureTypes[slotIndex]    = 2;
  memoryImages[slotIndex]    = std::vector<std::shared_ptr<MemoryImage>>();
  resources[slotIndex]       = std::vector<std::shared_ptr<Resource>>();
}


std::vector<std::shared_ptr<Resource>>& TextureRegistryArrayOfTextures::getResources(uint32_t slotIndex)
{
  auto it = resources.find(slotIndex);
  CHECK_LOG_THROW(it == end(resources), "There's no resource registered. Slot index " << slotIndex);
  return it->second;
}

void TextureRegistryArrayOfTextures::setTexture(uint32_t slotIndex, uint32_t layerIndex, std::shared_ptr<gli::texture> tex)
{
  auto it = memoryImages.find(slotIndex);
  CHECK_LOG_THROW(it == end(memoryImages), "There's no textures registered. Slot index " << slotIndex);
  auto rit = resources.find(slotIndex);

  if (layerIndex >= it->second.size())
  {
    it->second.resize(layerIndex + 1);
    rit->second.resize(layerIndex + 1);
  }
  // this texture will not be modified by GPU, so it is enough to declare it as swOnce
  switch (textureTypes[slotIndex])
  {
  case 0: // combined image samplers
    it->second[layerIndex]  = std::make_shared<MemoryImage>(tex, textureAllocator, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT, pbPerDevice);
    rit->second[layerIndex] = std::make_shared<CombinedImageSampler>(std::make_shared<ImageView>(it->second[layerIndex], it->second[layerIndex]->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D), textureSamplers[slotIndex]);
    break;
  case 1: // sampled images
    it->second[layerIndex]  = std::make_shared<MemoryImage>(tex, textureAllocator, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT, pbPerDevice);
    rit->second[layerIndex] = std::make_shared<SampledImage>(std::make_shared<ImageView>(it->second[layerIndex], it->second[layerIndex]->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D));
    break;
  case 2: // storage images
    it->second[layerIndex]  = std::make_shared<MemoryImage>(tex, textureAllocator, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_STORAGE_BIT, pbPerDevice);
    rit->second[layerIndex] = std::make_shared<StorageImage>(std::make_shared<ImageView>(it->second[layerIndex], it->second[layerIndex]->getFullImageRange(), VK_IMAGE_VIEW_TYPE_2D));
    break;
  }
}
