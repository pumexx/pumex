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