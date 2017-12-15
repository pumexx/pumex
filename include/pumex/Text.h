//
// Copyright(c) 2017 Paweł Księżopolski ( pumexx )
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
#include <pumex/UniformBuffer.h>
#include <pumex/StorageBuffer.h>
#include <pumex/GenericBufferPerSurface.h>

namespace pumex
{

class Texture;
class DeviceMemoryAllocator;

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


class PUMEX_EXPORT Font
{
public:
  Font()                       = delete;
  explicit Font(const std::string& fileName, glm::uvec2 textureSize, uint32_t fontPixelHeight, std::weak_ptr<DeviceMemoryAllocator> textureAllocator, std::weak_ptr<DeviceMemoryAllocator> bufferAllocator);
  Font(const Font&)            = delete;
  Font& operator=(const Font&) = delete;
  virtual ~Font();

  void validate(const RenderContext& renderContext);
  void addSymbolData(const glm::vec2& startPosition, const glm::vec4& color, const std::wstring& text, std::vector<SymbolData>& symbolData);

  std::shared_ptr<Texture>     fontTexture;
  std::vector<GlyphData>       glyphData;
protected:
  size_t getGlyphIndex(wchar_t charCode);

  mutable std::mutex mutex;
  static FT_Library  fontLibrary;
  static uint32_t    fontCount;
  FT_Face            fontFace = nullptr;

  gli::texture2d                      fontTexture2d;
  std::unordered_map<wchar_t, size_t> registeredGlyphs;
  glm::uvec2                          textureSize;
  uint32_t                            fontPixelHeight;
  glm::uvec2                          lastRegisteredPosition;
};

class PUMEX_EXPORT Text
{
public:
  Text()                       = delete;
  explicit Text(std::weak_ptr<Font> f, std::weak_ptr<DeviceMemoryAllocator> ba);
  Text(const Text&)            = delete;
  Text& operator=(const Text&) = delete;
  virtual ~Text();

  inline void     setActiveIndex(uint32_t index);
  inline uint32_t getActiveIndex() const;
  void            validate(Surface* surface);
  void            cmdDraw(Surface* surface, std::shared_ptr<CommandBuffer> commandBuffer) const;

  void            setText(Surface* surface, uint32_t index, const glm::vec2& position, const glm::vec4& color, const std::wstring& text);
  void            removeText(Surface* surface, uint32_t index);
  void            clearTexts();
  inline void     setDirty();

  std::shared_ptr<GenericBufferPerSurface<std::vector<SymbolData>>> vertexBuffer;
  std::vector<VertexSemantic>                                       textVertexSemantic;
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


  mutable std::mutex                                                                mutex;
  bool                                                                              dirty;
  std::weak_ptr<Font>                                                               font;
  std::unordered_map<VkSurfaceKHR,std::shared_ptr<std::vector<SymbolData>>>         symbolData;
  std::map<TextKey, std::tuple<glm::vec2, glm::vec4, std::wstring>, TextKeyCompare> texts;
};

void     Text::setActiveIndex(uint32_t index) { vertexBuffer->setActiveIndex(index); }
uint32_t Text::getActiveIndex() const         { return vertexBuffer->getActiveIndex(); }
void     Text::setDirty()                     { dirty = true; vertexBuffer->invalidate(); }


}