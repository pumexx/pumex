#pragma once
#include <memory>
#include <map>
#include <pumex/Export.h>
#include <glm/glm.hpp>

namespace pumex
{

// In Vulkan Y coordinate is directed downwards the screen as oposed to OpenGL (upwards the screen)
// To facilitate this we premultiply projection matrix by below defined correction matrix
const glm::mat4 vulkanPerspectiveCorrectionMatrix(1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);

// class that represents camera object and may be transfered to GPU for use in shaders
class PUMEX_EXPORT Camera
{
public:
  explicit Camera() = default;
  explicit Camera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec4& pos, float timeSinceStart);

  void             setViewMatrix( const glm::mat4& matrix );
  inline glm::mat4 getViewMatrix() const;
  inline glm::mat4 getViewMatrixInverse() const;

  void             setProjectionMatrix(const glm::mat4& matrix);
  inline glm::mat4 getProjectionMatrix() const;

  void             setObserverPosition(const glm::vec3& pos);
  void             setObserverPosition( const glm::vec4& pos );
  inline glm::vec4 getObserverPosition() const;

  void             setTimeSinceStart(float timeSinceStart);
  inline float     getTimeSinceStart() const;

protected:
  glm::mat4 viewMatrix;
  glm::mat4 viewMatrixInverse;
  glm::mat4 projectionMatrix;
  glm::vec4 observerPosition; // used for LOD computations. Usually the same as in viewMatrix
  float     timeSinceStart;  // FIXME - check std430 padding in a spare time
};

glm::mat4 Camera::getViewMatrix() const        { return viewMatrix; }
glm::mat4 Camera::getViewMatrixInverse() const { return viewMatrixInverse; }
glm::mat4 Camera::getProjectionMatrix() const  { return projectionMatrix * vulkanPerspectiveCorrectionMatrix; }
glm::vec4 Camera::getObserverPosition() const  { return observerPosition; }
float     Camera::getTimeSinceStart() const    { return timeSinceStart; }

}