//
// Copyright(c) 2017-2019 Paweł Księżopolski ( pumexx )
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

#include <pumex/utils/ReadFile.h>
#include <fstream>
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
  #include <android/asset_manager.h> // so we can use AAssetManager
  #include <android_native_app_glue.h>
  #include <pumex/platform/android/WindowAndroid.h>
#endif
#include <pumex/utils/Log.h>

namespace pumex
{

void readFileToMemory( const std::string& fileName, std::vector<unsigned char>& fileContents )
{
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
  if( fileName.find_first_of("/\\") != 0 )
  {
    AAsset* asset = AAssetManager_open(WindowAndroid::getAndroidApp()->activity->assetManager, fileName.c_str(), AASSET_MODE_STREAMING);
    CHECK_LOG_THROW(asset == nullptr, "Cannot load file from APK : " << fileName);
    size_t fileSize = AAsset_getLength(asset);
    CHECK_LOG_THROW(fileSize == 0, "Cannot load file from APK - size is 0 : " << fileName);
	
    fileContents.resize(fileSize);
    AAsset_read(asset, fileContents.data(), fileContents.size());
    AAsset_close(asset);
  }
  else
#endif
  {
    std::ifstream file(fileName.c_str(), std::ios::in | std::ios::binary);
    CHECK_LOG_THROW(!file, "Cannot open file " << fileName);

    // read all file contents into a fileContents vector
    auto fileBegin = file.tellg();
    file.seekg(0, std::ios::end);
    auto fileEnd = file.tellg();
    file.seekg(0, std::ios::beg);
    std::size_t fileSize = static_cast<std::size_t>(fileEnd - fileBegin);
    CHECK_LOG_THROW(fileSize == 0, "Cannot load file - size is 0 : " << fileName);
    fileContents.resize(fileSize);
    file.read(reinterpret_cast<char*>(fileContents.data()), fileSize);
    CHECK_LOG_THROW(file.fail(), "Cannot load file " << fileName);
  }
}

}
