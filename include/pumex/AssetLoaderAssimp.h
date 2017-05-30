#pragma once
#include <pumex/Export.h>
#include <pumex/Asset.h>
#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>


namespace pumex
{

// asset loader that uses Assimp library
class PUMEX_EXPORT AssetLoaderAssimp : public AssetLoader
{
public:
  explicit AssetLoaderAssimp();
  pumex::Asset* load(const std::string& fileName, bool animationOnly = false, const std::vector<pumex::VertexSemantic>& requiredSemantic = std::vector<pumex::VertexSemantic>()) override;

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