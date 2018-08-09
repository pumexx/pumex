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

#include <pumex/Text.h>
#include <pumex/NodeVisitor.h>
#include <pumex/MemoryImage.h>
#include <pumex/utils/Log.h>
#include <pumex/Surface.h>
#include <pumex/MemoryBuffer.h>
#include <pumex/Viewer.h>

using namespace pumex;

FT_Library Font::fontLibrary = nullptr;
uint32_t Font::fontCount = 0;

const uint32_t PUMEX_GLYPH_MARGIN = 4;

Font::Font(std::shared_ptr<Viewer> viewer, const std::string& fileName, glm::ivec2 ts, uint32_t fph, std::shared_ptr<DeviceMemoryAllocator> textureAllocator)
  : textureSize{ ts }, fontPixelHeight{ fph }
{
  if (fontLibrary == nullptr)
    FT_Init_FreeType(&fontLibrary);
  auto fullFileName = viewer->getAbsoluteFilePath(fileName);
  CHECK_LOG_THROW(fullFileName.empty(), "Cannot find font file : " << fileName);
  CHECK_LOG_THROW( FT_New_Face(fontLibrary, fullFileName.c_str(), 0, &fontFace) != 0, "Cannot load font file : " << fullFileName);
  fontCount++;

  FT_Set_Pixel_Sizes(fontFace, 0, fontPixelHeight);
  fontTexture2d = std::make_shared<gli::texture2d>(gli::format::FORMAT_R8_UNORM_PACK8, gli::texture2d::extent_type(textureSize), 1);
  fontTexture2d->clear<gli::u8>(0);
  fontMemoryImage = std::make_shared<MemoryImage>(fontTexture2d, textureAllocator, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT, pbPerDevice);

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
  for (auto& c : text)
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

size_t Font::getGlyphIndex(wchar_t charCode)
{
  auto it = registeredGlyphs.find(charCode);
  if ( it != end(registeredGlyphs))
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

  gli::image fontImage = (*fontTexture2d)[0];
  // copy freetype bitmap to a texture
  for (unsigned int i = 0; i < fontFace->glyph->bitmap.rows; ++i)
  {
    gli::extent2d firstTexel(lastRegisteredPosition.x, lastRegisteredPosition.y + i);
    gli::u8* dstData = fontImage.data<gli::u8>() + (firstTexel.x + textureSize.x * firstTexel.y);
    gli::u8* srcData = fontFace->glyph->bitmap.buffer + fontFace->glyph->bitmap.width * i;
    std::memcpy(dstData, srcData, fontFace->glyph->bitmap.width);
  }
  fontMemoryImage->invalidateImage();
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

Text::Text(std::shared_ptr<Font> f, std::shared_ptr<DeviceMemoryAllocator> bufferAllocator)
  : DrawNode(), font{ f }
{
  vertexBuffer = std::make_shared<Buffer<std::vector<SymbolData>>>(bufferAllocator, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, pbPerSurface, swForEachImage);
  textVertexSemantic = { { VertexSemantic::Position, 4 },{ VertexSemantic::TexCoord, 4 } , { VertexSemantic::Color, 4 } };
}

Text::~Text()
{
}

void Text::validate(const RenderContext& renderContext)
{
  if (!registered)
  {
    font->fontMemoryImage->addCommandBufferSource(shared_from_this());
    vertexBuffer->addCommandBufferSource(shared_from_this());
    registered = true;
  }

  auto sit = symbolData.find(renderContext.vkSurface);
  if (sit == end(symbolData))
    sit = symbolData.insert({ renderContext.vkSurface, std::make_shared<std::vector<SymbolData>>() }).first;

  uint32_t oldDataSize = sit->second->size();
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
    vertexBuffer->setData(renderContext.surface, sit->second);
  }
  if (sit->second->size() != oldDataSize)
    notifyCommandBuffers();
  vertexBuffer->validate(renderContext);
}

void Text::cmdDraw(const RenderContext& renderContext, CommandBuffer* commandBuffer)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto sit = symbolData.find(renderContext.vkSurface);
  CHECK_LOG_THROW(sit == end(symbolData), "Text::cmdDraw() : text was not validated");
  if (sit->second.get() == nullptr || sit->second->empty())
    return;

  commandBuffer->addSource(this);
  VkBuffer     vBuffer = vertexBuffer->getHandleBuffer(renderContext);
  VkDeviceSize offsets = 0;
  vkCmdBindVertexBuffers(commandBuffer->getHandle(), 0, 1, &vBuffer, &offsets);
  commandBuffer->cmdDraw(sit->second->size(), 1, 0, 0, 0);
}

void Text::setText(Surface* surface, uint32_t index, const glm::vec2& position, const glm::vec4& color, const std::wstring& text)
{
  std::lock_guard<std::mutex> lock(mutex);
  texts[TextKey(surface->surface,index)] = std::make_tuple(position, color, text);
  invalidateNodeAndParents(surface);
}

void Text::removeText(Surface* surface, uint32_t index)
{
  std::lock_guard<std::mutex> lock(mutex);
  texts.erase(TextKey(surface->surface, index));
  invalidateNodeAndParents(surface);
}

void Text::clearTexts()
{
  std::lock_guard<std::mutex> lock(mutex);
  texts.clear();
  invalidateNodeAndParents();
}
