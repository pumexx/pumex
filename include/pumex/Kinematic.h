#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace pumex
{

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

Kinematic interpolate(const Kinematic& object0, const Kinematic& object1, float interpolation);

glm::mat4 extrapolate(const Kinematic& kinematic, float deltaTime);

}