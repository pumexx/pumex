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
#include <glm/glm.hpp>
#include <pumex/Export.h>

namespace pumex
{

  
struct PUMEX_EXPORT BoundingBox
{
  BoundingBox()
    : bbMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), bbMax(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest())
  {
  }
  BoundingBox(const glm::vec3& mn, const glm::vec3& mx)
    : bbMin(mn), bbMax(mx)
  {
  }

  void operator+=(const glm::vec3& v)
  {
    if (v.x<bbMin.x) bbMin.x = v.x;
    if (v.x>bbMax.x) bbMax.x = v.x;

    if (v.y<bbMin.y) bbMin.y = v.y;
    if (v.y>bbMax.y) bbMax.y = v.y;

    if (v.z<bbMin.z) bbMin.z = v.z;
    if (v.z>bbMax.z) bbMax.z = v.z;
  }

  void operator+=(const BoundingBox& bbox)
  {
    if (bbox.bbMin.x<bbMin.x) bbMin.x = bbox.bbMin.x;
    if (bbox.bbMax.x>bbMax.x) bbMax.x = bbox.bbMax.x;

    if (bbox.bbMin.y<bbMin.y) bbMin.y = bbox.bbMin.y;
    if (bbox.bbMax.y>bbMax.y) bbMax.y = bbox.bbMax.y;

    if (bbox.bbMin.z<bbMin.z) bbMin.z = bbox.bbMin.z;
    if (bbox.bbMax.z>bbMax.z) bbMax.z = bbox.bbMax.z;
  }

  bool contains(const glm::vec3& v) const
  {
    return (v.x >= bbMin.x && v.x <= bbMax.x) && (v.y >= bbMin.y && v.y <= bbMax.y) && (v.z >= bbMin.z && v.z <= bbMax.z);
  }

  float radius() const
  {
    return 0.5 * sqrt(glm::length(bbMax - bbMin));
  }

  glm::vec3 center() const
  {
    return (bbMax + bbMin) * 0.5f;
  }


  glm::vec3 bbMin;
  glm::vec3 bbMax;
};


}