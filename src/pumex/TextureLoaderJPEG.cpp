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

#include <pumex/TextureLoaderJPEG.h>
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
#include <jpeglib.h>
#include <pumex/utils/Log.h>

using namespace pumex;

TextureLoaderJPEG::TextureLoaderJPEG()
  : TextureLoader{ {"jpg", "jpeg"} }
{
}

std::shared_ptr<gli::texture> TextureLoaderJPEG::load(const std::string& fileName, bool buildMipMaps)
{
  std::vector<unsigned char> jpegContents;
  {
    // open jpeg file
    std::ifstream file(fileName.c_str(), std::ios::in | std::ios::binary);
    CHECK_LOG_THROW(!file, "Cannot open JPEG file " << fileName);

    // read all file contents into a jpegContents vector
    auto fileBegin = file.tellg();
    file.seekg(0, std::ios::end);
    auto fileEnd = file.tellg();
    file.seekg(0, std::ios::beg);
    std::size_t fileSize = static_cast<std::size_t>(fileEnd - fileBegin);
    jpegContents.resize(fileSize);
    file.read(reinterpret_cast<char*>(jpegContents.data()), fileSize);
    CHECK_LOG_THROW(file.fail(), "Failed to read JPEG file " << fileName);
  }

  jpeg_decompress_struct cinfo;
  jpeg_error_mgr         jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, jpegContents.data(), jpegContents.size());
  int rc = jpeg_read_header(&cinfo, TRUE);
  CHECK_LOG_THROW(rc != 1, "Header says, that this is not JPEG file  " << fileName);
  jpeg_start_decompress(&cinfo);

  uint32_t width      = cinfo.output_width;
  uint32_t height     = cinfo.output_height;
  uint32_t pixel_size = cinfo.output_components;

  gli::format fmt   = gli::FORMAT_UNDEFINED;
  bool resizeRGB2RGBA = false;
  switch (pixel_size)
  {
  case 1: fmt = gli::FORMAT_R8_UNORM_PACK8; break;
  case 2: fmt = gli::FORMAT_RG8_UNORM_PACK8;  break;
  case 3: resizeRGB2RGBA = true;
  case 4: fmt = gli::FORMAT_RGBA8_UNORM_PACK8; break;
  default: break;
  }
  CHECK_LOG_THROW(fmt == gli::FORMAT_UNDEFINED, "Cannot recognize pixel format " << fileName);

  gli::texture2d level0(fmt, gli::texture2d::extent_type(width, height), 1);
  gli::texture::size_type blockSize = gli::block_size(fmt);
  gli::texture::size_type lineSize = blockSize * width;

  unsigned char* imageStart = static_cast<unsigned char*>(level0.data());
  std::vector<unsigned char> decodedLine;
  decodedLine.resize(pixel_size * width);

  for( uint32_t i=0; i<height; ++i)
  {
    unsigned char* readPixels = resizeRGB2RGBA ? decodedLine.data() : imageStart + (height-i-1) * lineSize;
    jpeg_read_scanlines(&cinfo, &readPixels, 1);
    if (!resizeRGB2RGBA)
      continue;
    for (uint32_t j=0; j < width; j++)
    {
      unsigned char* source = decodedLine.data() + 3 * j;
      unsigned char* target = imageStart + (height - i - 1) * lineSize + 4 * j;
      target[0] = source[0];
      target[1] = source[1];
      target[2] = source[2];
      target[3] = 0xFF;
    }
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  std::shared_ptr<gli::texture> texture;
  if(buildMipMaps)
    texture = std::make_shared<gli::texture2d>(gli::generate_mipmaps(level0, gli::FILTER_LINEAR));
  else
    texture = std::make_shared<gli::texture2d>(level0);
  return texture;
}
