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

#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <ft2build.h>
#include FT_FREETYPE_H  
#include <glm/glm.hpp>
#include <gli/texture2d.hpp>
#include <pumex/Export.h>
#include <pumex/Asset.h>
#include <pumex/Node.h>

namespace pumex
{

class MemoryImage;
class DeviceMemoryAllocator;
template <typename T> class Buffer;

struct PUMEX_EXPORT GlyphData
{
  GlyphData() = default;
  GlyphData(glm::vec4 t, glm::vec4 b, float a)
    : texCoords{ t }, bearing{ b }, advance{ a }
  {}
  glm::vec4 texCoords; // left, top, left + width, top + rows - all divided by texture dimensions
  glm::vec4 bearing;   // bearingX, bearingY, bearingX + width, bearingY + height
  float     advance;   
};

struct PUMEX_EXPORT SymbolData
{
  SymbolData() = default;
  SymbolData( const glm::vec4& p, const glm::vec4& t, const glm::vec4& c)
    : position{ p }, texCoords{ t }, color{ c } 
  {}
  glm::vec4 position;  // left, top, right, bottom
  glm::vec4 texCoords; // left, top, left + width, top + rows - all divided by texture dimensions
  glm::vec4 color;     // font color
};

// class that stores Font texture and glyph data in memory ( ready to send it to GPU )
class PUMEX_EXPORT Font
{
public:
  Font()                       = delete;
  explicit Font(const filesystem::path& fileName, glm::ivec2 textureSize, uint32_t fontPixelHeight, std::shared_ptr<DeviceMemoryAllocator> textureAllocator);
  Font(const Font&)            = delete;
  Font& operator=(const Font&) = delete;
  Font(Font&&)                 = delete;
  Font& operator=(Font&&)      = delete;
  virtual ~Font();

  void addSymbolData(const glm::vec2& startPosition, const glm::vec4& color, const std::wstring& text, std::vector<SymbolData>& symbolData);

  std::shared_ptr<MemoryImage> fontMemoryImage;
  std::vector<GlyphData>       glyphData;
protected:
  size_t getGlyphIndex(wchar_t charCode);

  mutable std::mutex mutex;
  static FT_Library  fontLibrary;
  static uint32_t    fontCount;
  FT_Face            fontFace = nullptr;

  std::shared_ptr<gli::texture2d>     fontTexture2d;
  std::unordered_map<wchar_t, size_t> registeredGlyphs;
  glm::ivec2                          textureSize;
  uint32_t                            fontPixelHeight;
  glm::ivec2                          lastRegisteredPosition;
};

// class that stores texts that may be written on screen
class PUMEX_EXPORT Text : public Node
{
public:
  Text()                       = delete;
  explicit Text(std::shared_ptr<Font> f, std::shared_ptr<DeviceMemoryAllocator> bufferAllocator);
  Text(const Text&)            = delete;
  Text& operator=(const Text&) = delete;
  Text(Text&&)                 = delete;
  Text& operator=(Text&&)      = delete;
  virtual ~Text();

  void accept(NodeVisitor& visitor) override;
  void validate(const RenderContext& renderContext) override;
  void cmdDraw(const RenderContext& renderContext, CommandBuffer* commandBuffer);

  void setText(Surface* surface, uint32_t index, const glm::vec2& position, const glm::vec4& color, const std::wstring& text);
  void removeText(Surface* surface, uint32_t index);
  void clearTexts();

  std::shared_ptr<Buffer<std::vector<SymbolData>>> vertexBuffer;
  std::vector<VertexSemantic>                      textVertexSemantic;
protected:
  struct TextKey
  {
    TextKey(VkSurfaceKHR s, uint32_t i)
      : surface{ s }, index{ i }
    {
    }
    VkSurfaceKHR surface;
    uint32_t     index;
  };
  struct TextKeyCompare
  {
    bool operator()(const TextKey& lhs, const TextKey& rhs) const
    {
      if (lhs.surface != rhs.surface)
        return lhs.surface < rhs.surface;
      return lhs.index < rhs.index;
    }
  };

  std::shared_ptr<Font>                                                             font;
  std::unordered_map<VkSurfaceKHR,std::shared_ptr<std::vector<SymbolData>>>         symbolData;
  std::map<TextKey, std::tuple<glm::vec2, glm::vec4, std::wstring>, TextKeyCompare> texts;
  bool                                                                              registered = false;
};

}