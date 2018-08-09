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
#include <functional>

// hash_combine and hash_value have been adopted from chapter 7.9.2 found in "The C++ Standard Library: A Tutorial and Reference (2nd Edition)" by Nicolai M. Josuttis
namespace std
{
  template <> struct hash<VkDescriptorType>
  {
    size_t operator()(const VkDescriptorType& x) const
    {
      return hash<int>()(x);
    }
  };
}

namespace pumex
{

template <typename T>
inline void hash_combine( std::size_t& seed, const T& value )
{
  seed ^= std::hash<T>()(value) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

template<typename T>
inline void hash_value( std::size_t& seed, const T& value )
{
  hash_combine(seed, value);
}

template<typename Head, typename... Tail>
inline void hash_value( std::size_t& seed, const Head& value, const Tail&... tail )
{
  hash_combine(seed, value);
  hash_value(seed, tail...);
}

template<typename... Types>
inline std::size_t hash_value(const Types&... args)
{
  std::size_t seed = 0;
  hash_value(seed, args...);
  return seed;
}

}
