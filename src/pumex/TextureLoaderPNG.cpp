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

#include <pumex/TextureLoaderPNG.h>
#if defined(GLM_ENABLE_EXPERIMENTAL) // hack around redundant GLM_ENABLE_EXPERIMENTAL defined in type.hpp
#undef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL_HACK
#endif
#include <gli/texture2d.hpp>
#include <gli/generate_mipmaps.hpp>
#if defined(GLM_ENABLE_EXPERIMENTAL_HACK)
#define GLM_ENABLE_EXPERIMENTAL
#undef GLM_ENABLE_EXPERIMENTAL_HACK
#endif
#include <fstream>
#include <png.h>
#include <pumex/utils/Log.h>

using namespace pumex;

class PNGMemoryStream
{
public:
  PNGMemoryStream(std::vector<png_byte>& pngc)
    : pngContents{ pngc }, position{ 0 }
  {
  }

  void read(png_structp read, png_bytep data, png_size_t length)
  {
    memcpy(data, pngContents.data() + position, length);
    position += length;
  }
private:
  std::vector<png_byte>& pngContents;
  png_size_t position = 0;
};

void readMemStream(png_structp pngPtr, png_bytep data, size_t length)
{
  PNGMemoryStream* memStream = static_cast<PNGMemoryStream*>( png_get_io_ptr(pngPtr) );
  memStream->read(pngPtr, data, length);
}

TextureLoaderPNG::TextureLoaderPNG()
  : TextureLoader{ {"png"} }
{
}

std::shared_ptr<gli::texture> TextureLoaderPNG::load(const std::string& fileName, bool buildMipMaps)
{
  std::vector<png_byte> pngContents;
  {
    // open png file
    std::ifstream file(fileName.c_str(), std::ios::in | std::ios::binary);
    CHECK_LOG_THROW(!file, "Cannot open PNG file " << fileName);

    // read all file contents into a pngContents vector
    auto fileBegin = file.tellg();
    file.seekg(0, std::ios::end);
    auto fileEnd = file.tellg();
    file.seekg(0, std::ios::beg);
    std::size_t fileSize = static_cast<std::size_t>(fileEnd - fileBegin);
    pngContents.resize(fileSize);
    file.read(reinterpret_cast<char*>(pngContents.data()), fileSize);
    CHECK_LOG_THROW(file.fail(), "Failed to read PNG file " << fileName);
  }

  png_structp pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  CHECK_LOG_THROW(pngPtr == nullptr, "Cannot create PNG read struct");

  png_infop infoPtr = png_create_info_struct(pngPtr);
  png_infop endInfoPtr = png_create_info_struct(pngPtr);
  if (infoPtr == nullptr || endInfoPtr==nullptr)
  {
    png_destroy_read_struct(&pngPtr, nullptr, nullptr);
    CHECK_LOG_THROW(true, "Cannot create PNG info struct");
  }

  PNGMemoryStream memStream(pngContents);
  png_set_read_fn(pngPtr, static_cast<png_voidp>(&memStream), readMemStream);

  png_byte signature[8];
  memStream.read(pngPtr, signature, 8);
  int signatureComp = png_sig_cmp(signature, 0, 8);
  CHECK_LOG_THROW(signatureComp != 0, "Signature says, that this is not PNG file " << fileName);

  png_set_sig_bytes(pngPtr, 8);
  png_read_info(pngPtr, infoPtr);
  png_uint_32 width  = png_get_image_width(pngPtr, infoPtr);
  png_uint_32 height = png_get_image_height(pngPtr, infoPtr);
  int bitDepth       = png_get_bit_depth(pngPtr, infoPtr);
  int colorType      = png_get_color_type(pngPtr, infoPtr);

  gli::format fmt = gli::FORMAT_UNDEFINED;
  switch (colorType)
  {
  case PNG_COLOR_TYPE_GRAY:
    switch (bitDepth)
    {
    case 1:
    case 2:
    case 4:  png_set_expand_gray_1_2_4_to_8(pngPtr);
    case 8:  fmt = gli::FORMAT_R8_UNORM_PACK8; break;
    case 16: fmt = gli::FORMAT_R16_UNORM_PACK16; break;
    default: break;
    }
    break;
  case PNG_COLOR_TYPE_GRAY_ALPHA:
    switch (bitDepth)
    {
    case 1:
    case 2:
    case 4:  png_set_expand_gray_1_2_4_to_8(pngPtr);
    case 8:  fmt = gli::FORMAT_RG8_UNORM_PACK8; break;
    case 16: fmt = gli::FORMAT_RG16_UNORM_PACK16; break;
    default: break;
    }
    break;
  case PNG_COLOR_TYPE_RGB:
    png_set_add_alpha(pngPtr, 0xffff, PNG_FILLER_AFTER);
  case PNG_COLOR_TYPE_RGB_ALPHA:
    switch (bitDepth)
    {
    case 8:  fmt = gli::FORMAT_RGBA8_UNORM_PACK8; break;
    case 16: fmt = gli::FORMAT_RGBA16_UNORM_PACK16; break;
    default: break;
    }
    break;
  case PNG_COLOR_TYPE_PALETTE:
    png_set_palette_to_rgb(pngPtr);
    fmt = gli::FORMAT_RGB8_UNORM_PACK8;
    break;
  }
  CHECK_LOG_THROW(fmt == gli::FORMAT_UNDEFINED, "Cannot recognize pixel format " << fileName);
  png_read_update_info(pngPtr, infoPtr);

  gli::texture2d level0(fmt, gli::texture2d::extent_type(width, height), 1);
  gli::texture::size_type blockSize = gli::block_size(fmt);
  gli::texture::size_type lineSize  = blockSize * width;

  png_bytep* rows                  = new png_bytep[height];
  png_bytep imageStart              = static_cast<png_bytep>(level0.data());
  for (uint32_t i=0; i<height; ++i)
    rows[i] = imageStart + (height-i-1) * lineSize;
  png_read_image(pngPtr, rows);
  delete[] rows;
  png_read_end(pngPtr, endInfoPtr);
  png_destroy_read_struct(&pngPtr, &infoPtr, &endInfoPtr);

  std::shared_ptr<gli::texture> texture;
  if(buildMipMaps)
    texture = std::make_shared<gli::texture2d>(gli::generate_mipmaps(level0, gli::FILTER_LINEAR));
  else
    texture = std::make_shared<gli::texture2d>(level0);
  return texture;
}
