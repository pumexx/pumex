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

#include <pumex/TextureLoaderGli.h>
#if defined(GLM_ENABLE_EXPERIMENTAL) // hack around redundant GLM_ENABLE_EXPERIMENTAL defined in type.hpp
  #undef GLM_ENABLE_EXPERIMENTAL
  #define GLM_ENABLE_EXPERIMENTAL_HACK
#endif
#include <gli/load.hpp>
#if defined(GLM_ENABLE_EXPERIMENTAL_HACK)
  #define GLM_ENABLE_EXPERIMENTAL
  #undef GLM_ENABLE_EXPERIMENTAL_HACK
#endif

using namespace pumex;

std::shared_ptr<gli::texture> TextureLoaderGli::load(const std::string& fileName)
{
  return std::make_shared<gli::texture>( gli::load(fileName) );
}
