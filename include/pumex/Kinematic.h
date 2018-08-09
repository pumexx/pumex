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
#include <glm/gtc/quaternion.hpp>
#include <pumex/Export.h>

namespace pumex
{

// struct storing position, orientation and velocity ( both linear and angular ) of a single 3D object
// Used during update phase and then extrapolated to glm::mat4 during rendering phase.
struct Kinematic
{
  Kinematic(const glm::vec3& p = glm::vec3(), const glm::quat& o = glm::quat(), const glm::vec3& v=glm::vec3(), const glm::vec3& r = glm::vec3())
    : position(p), orientation(o), velocity(v), angularVelocity(r)
  {
  }
  Kinematic(const Kinematic& k)
    : position(k.position), orientation(k.orientation), velocity(k.velocity), angularVelocity(k.angularVelocity)
  {
  }

  Kinematic& operator=(const Kinematic& k)
  {
    if (this != &k)
    {
      position        = k.position;
      orientation     = k.orientation;
      velocity        = k.velocity;
      angularVelocity = k.angularVelocity;
    }
    return *this;
  }
  glm::vec3	position;
  glm::quat	orientation;
  glm::vec3	velocity;
  glm::vec3	angularVelocity;
};

PUMEX_EXPORT Kinematic interpolate(const Kinematic& object0, const Kinematic& object1, float interpolation);

PUMEX_EXPORT glm::mat4 extrapolate(const Kinematic& kinematic, float deltaTime);

PUMEX_EXPORT void calculateVelocitiesFromPositionOrientation(Kinematic& current, const Kinematic& previous, float deltaTime);


}
