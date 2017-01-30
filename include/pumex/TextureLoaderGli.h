#pragma once
#include <pumex/Texture.h>

namespace pumex
{
	
// texture loader that is able to load DDS and KTX textures using GLI library
class PUMEX_EXPORT TextureLoaderGli : public TextureLoader
{
public:
  explicit TextureLoaderGli() = default;
  std::shared_ptr<gli::texture> load(const std::string& fileName) override;
};

}