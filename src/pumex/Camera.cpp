#include <pumex/Camera.h>

using namespace pumex;

Camera::Camera(const glm::mat4& vm, const glm::mat4& pm, const glm::vec4& p, float t)
  : viewMatrix{ vm }, projectionMatrix{ pm }, observerPosition{p}, timeSinceStart{ t }
{
}

void Camera::setViewMatrix(const glm::mat4& matrix)
{
  viewMatrix = matrix;
  viewMatrixInverse = glm::inverse(matrix);
}

void Camera::setProjectionMatrix(const glm::mat4& matrix)
{
  projectionMatrix = vulkanPerspectiveCorrectionMatrix * matrix;
}

void Camera::setObserverPosition(const glm::vec4& pos)
{
  observerPosition = pos;
}

void Camera::setObserverPosition(const glm::vec3& pos)
{
  observerPosition.x = pos.x;
  observerPosition.y = pos.y;
  observerPosition.z = pos.z;
  observerPosition.w = 1.0f;
}

void Camera::setTimeSinceStart(float tss)
{
  timeSinceStart = tss;
}

