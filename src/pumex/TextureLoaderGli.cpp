#include <pumex/TextureLoaderGli.h>
#include <gli/load.hpp>

using namespace pumex;

std::shared_ptr<gli::texture> TextureLoaderGli::load(const std::string& fileName)
{
	return std::make_shared<gli::texture>( gli::load(fileName) );
}
