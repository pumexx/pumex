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

#include <pumex/Kinematic.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace pumex
{

Kinematic interpolate(const Kinematic& object0, const Kinematic& object1, float interpolation)
{
  return Kinematic(
    glm::mix  (object0.position, object1.position, interpolation),
    glm::slerp(object0.orientation, object1.orientation, interpolation),
    glm::mix  (object0.velocity, object1.velocity, interpolation),
    glm::mix  (object0.angularVelocity, object1.angularVelocity, interpolation)); // r u sure ?
}

glm::mat4 extrapolate(const Kinematic& kinematic, float deltaTime)
{
  glm::vec3 position = kinematic.position + kinematic.velocity * deltaTime;
  glm::quat rotation(0.0f, kinematic.angularVelocity * deltaTime);
  glm::quat orientation = kinematic.orientation + rotation * kinematic.orientation * 0.5f;
  return glm::translate(glm::mat4(), position) * glm::mat4_cast(orientation);
}

}