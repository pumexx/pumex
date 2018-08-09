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

#pragma once
#include <assimp/Importer.hpp> 
#include <memory>
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <pumex/Export.h>
#include <pumex/Asset.h>

namespace pumex
{

// asset loader that uses Assimp library
class PUMEX_EXPORT AssetLoaderAssimp : public AssetLoader
{
public:
  explicit AssetLoaderAssimp();
  std::shared_ptr<Asset> load(std::shared_ptr<Viewer> viewer, const std::string& fileName, bool animationOnly = false, const std::vector<VertexSemantic>& requiredSemantic = std::vector<VertexSemantic>()) override;

  inline unsigned int getImportFlags() const;
  inline void setImportFlags(unsigned int flags);
protected:
  Assimp::Importer Importer;
  unsigned int     importFlags = aiProcess_Triangulate | aiProcess_SortByPType | aiProcess_JoinIdenticalVertices; //  aiPostProcessSteps
  //  unsigned int flags = aiProcess_FlipWindingOrder | aiProcess_Triangulate | aiProcess_PreTransformVertices | aiProcess_SortByPType; //  aiPostProcessSteps

};

unsigned int AssetLoaderAssimp::getImportFlags() const     { return importFlags; }
void AssetLoaderAssimp::setImportFlags(unsigned int flags) { importFlags = flags; }

}
