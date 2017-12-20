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

#include <pumex/Text.h>
#include <pumex/Texture.h>
#include <pumex/utils/Log.h>
#include <pumex/utils/Shapes.h>
#include <pumex/Surface.h>

/*
std::string fullFontFileName = viewer->getFullFilePath("fonts/DejaVuSans.ttf");
if (fullFontFileName.empty())
{
  // file not exists
}

std::shared_ptr<Font> font = std::make_shared<Font>(fullFontFileName, 12, glm::vec2( 2048,2048 ), allocator );
std::shared_ptr<Text> text = std::make_shared<Text>( font, glm::vec3(1.0f,0.0f,0.0f), glm::vec2(0,100), L"Napis jakiœ" );
text->setText(L"Napis inny");
text->validate(device);


text->draw();
*/

using namespace pumex;

FT_Library Font::fontLibrary = nullptr;
uint32_t Font::fontCount = 0;

const uint32_t PUMEX_GLYPH_MARGIN = 4;


Font::Font(const std::string& fileName, glm::uvec2 ts, uint32_t fph, std::weak_ptr<DeviceMemoryAllocator> textureAllocator, std::weak_ptr<DeviceMemoryAllocator> bufferAllocator)
  : textureSize{ ts }, fontPixelHeight{ fph }
{
  std::lock_guard<std::mutex> lock(mutex);
  if (fontLibrary == nullptr)
    FT_Init_FreeType(&fontLibrary);
  CHECK_LOG_THROW( FT_New_Face(fontLibrary, fileName.c_str(), 0, &fontFace) != 0, "Cannot load a font : " << fileName);
  fontCount++;

  FT_Set_Pixel_Sizes(fontFace, 0, fontPixelHeight);
  fontTexture   = std::make_shared<Texture>(gli::texture(gli::target::TARGET_2D, gli::format::FORMAT_R8_UNORM_PACK8, gli::texture::extent_type(textureSize.x, textureSize.y, 1), 1, 1, 1), SamplerTraits(), textureAllocator);
  fontTexture->texture->clear<gli::u8>(0);
  fontTexture2d = gli::texture2d( (*(fontTexture->texture.get())) );

  lastRegisteredPosition = glm::ivec2(PUMEX_GLYPH_MARGIN, PUMEX_GLYPH_MARGIN);
  // register first 128 char codes
  for (wchar_t c = 0; c < 128; ++c)
    getGlyphIndex(c);
}

Font::~Font()
{
  std::lock_guard<std::mutex> lock(mutex);
  FT_Done_Face(fontFace);
  fontCount--;
  if (fontCount == 0)
  {
    FT_Done_FreeType(fontLibrary);
    fontLibrary = nullptr;
  }
}

void Font::addSymbolData(const glm::vec2& startPosition, const glm::vec4& color, const std::wstring& text, std::vector<SymbolData>& symbolData)
{
  std::lock_guard<std::mutex> lock(mutex);
  glm::vec4 currentPosition(startPosition.x, startPosition.y, startPosition.x, startPosition.y);
  for (auto c : text)
  {
    GlyphData& gData = glyphData[getGlyphIndex(c)];
    symbolData.emplace_back( SymbolData(
      currentPosition + gData.bearing,
      gData.texCoords,
      color
      ));
    currentPosition.x += gData.advance;
    currentPosition.z += gData.advance;
  }
}


void Font::validate(const RenderContext& renderContext)
{
  fontTexture->validate(renderContext);
}

size_t Font::getGlyphIndex(wchar_t charCode)
{
  auto it = registeredGlyphs.find(charCode);
  if ( it != registeredGlyphs.end())
    return it->second;

  // load glyph from freetype
  CHECK_LOG_THROW(FT_Load_Char(fontFace, charCode, FT_LOAD_RENDER) != 0, "Cannot load glyph " << charCode);

  // find a place for a new glyph on a texture
  if (( lastRegisteredPosition.x + fontFace->glyph->bitmap.width ) >= (textureSize.x - PUMEX_GLYPH_MARGIN) )
  {
    lastRegisteredPosition.x = PUMEX_GLYPH_MARGIN;
    lastRegisteredPosition.y += fontPixelHeight + PUMEX_GLYPH_MARGIN;
    CHECK_LOG_THROW(lastRegisteredPosition.y >= textureSize.y, "out of memory for a new glyph");
  }

  gli::image fontImage = fontTexture2d[0];
  // copy freetype bitmap to a texture
  for (unsigned int i = 0; i < fontFace->glyph->bitmap.rows; ++i)
  {
    gli::extent2d firstTexel(lastRegisteredPosition.x, lastRegisteredPosition.y + i);
    gli::u8* dstData = fontImage.data<gli::u8>() + (firstTexel.x + textureSize.x * firstTexel.y);
    gli::u8* srcData = fontFace->glyph->bitmap.buffer + fontFace->glyph->bitmap.width * i;
    memcpy(dstData, srcData, fontFace->glyph->bitmap.width);
  }
  fontTexture->invalidate();
  glyphData.emplace_back( GlyphData(
    glm::vec4(
      (float)lastRegisteredPosition.x / (float)textureSize.x,
      (float)lastRegisteredPosition.y / (float)textureSize.y,
      (float)(lastRegisteredPosition.x + fontFace->glyph->bitmap.width) / (float)textureSize.x,
      (float)(lastRegisteredPosition.y + fontFace->glyph->bitmap.rows) / (float)textureSize.y
    ),
    glm::vec4( 
      fontFace->glyph->bitmap_left, 
      -1.0*fontFace->glyph->bitmap_top,
      fontFace->glyph->bitmap_left + fontFace->glyph->bitmap.width,
      -1.0*fontFace->glyph->bitmap_top + fontFace->glyph->bitmap.rows
    ), 
    fontFace->glyph->advance.x / 64.0f
  ) );

  lastRegisteredPosition.x += fontFace->glyph->bitmap.width + PUMEX_GLYPH_MARGIN;

  registeredGlyphs.insert({ charCode, glyphData.size() - 1 });
  return glyphData.size()-1;
}


Text::Text(std::shared_ptr<Font> f, std::shared_ptr<DeviceMemoryAllocator> ba)
  : Node(), font{ f }
{
  vertexBuffer = std::make_shared<GenericBufferPerSurface<std::vector<SymbolData>>>(ba, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  textVertexSemantic = { { VertexSemantic::Position, 4 },{ VertexSemantic::TexCoord, 4 } , { VertexSemantic::Color, 4 } };
}

Text::~Text()
{
}

void Text::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto sit = symbolData.find(renderContext.vkSurface);
  if (sit == symbolData.end())
  {
    sit = symbolData.insert({ renderContext.vkSurface, std::make_shared<std::vector<SymbolData>>() }).first;
    vertexBuffer->set(renderContext.surface, sit->second);
  }

  if (!valid)
  {
    sit->second->resize(0);

    for (const auto& t : texts)
    {
      if (t.first.surface != sit->first)
        continue;
      glm::vec2    startPosition;
      glm::vec4    color;
      std::wstring text;
      std::tie(startPosition, color, text) = t.second;
      font->addSymbolData(startPosition, color, text, *(sit->second));
    }
    vertexBuffer->invalidate();
    valid = true;
  }
  vertexBuffer->validate(renderContext);
}

void Text::cmdDraw(const RenderContext& renderContext, CommandBuffer* commandBuffer) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto sit = symbolData.find(renderContext.vkSurface);
  CHECK_LOG_THROW(sit == symbolData.end(), "Text::cmdDraw() : text was not validated");

  VkBuffer     vBuffer = vertexBuffer->getBufferHandle(renderContext);
  VkDeviceSize offsets = 0;
  commandBuffer->addSource(vertexBuffer.get());
  vkCmdBindVertexBuffers(commandBuffer->getHandle(), 0, 1, &vBuffer, &offsets);
  commandBuffer->cmdDraw(sit->second->size(), 1, 0, 0, 0);
}

void Text::setText(Surface* surface, uint32_t index, const glm::vec2& position, const glm::vec4& color, const std::wstring& text)
{
  std::lock_guard<std::mutex> lock(mutex);
  texts[TextKey(surface->surface,index)] = std::make_tuple(position, color, text);
  internalInvalidate();
}

void Text::removeText(Surface* surface, uint32_t index)
{
  std::lock_guard<std::mutex> lock(mutex);
  texts.erase(TextKey(surface->surface, index));
  internalInvalidate();
}

void Text::clearTexts()
{
  std::lock_guard<std::mutex> lock(mutex);
  texts.clear();
  internalInvalidate();
}



