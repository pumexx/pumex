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

#include <pumex/Camera.h>

using namespace pumex;

Camera::Camera(const glm::mat4& vm, const glm::mat4& pm, const glm::vec4& p, float t)
  : viewMatrix{ vm }, projectionMatrix{ pm }, observerPosition{p}, params{ t, 0.0f, 0.0f, 0.0f }
{
}

void Camera::setViewMatrix(const glm::mat4& matrix)
{
  viewMatrix = matrix;
  viewMatrixInverse = glm::inverse(matrix);
}

void Camera::setProjectionMatrix(const glm::mat4& matrix, bool usePerspectiveCorrection )
{
  if(usePerspectiveCorrection)
    projectionMatrix = vulkanPerspectiveCorrectionMatrix * matrix;
  else
    projectionMatrix = matrix;
}

glm::mat4 Camera::getProjectionMatrix(bool usePerspectiveCorrection) const 
{ 
  if(usePerspectiveCorrection)
    return projectionMatrix * vulkanPerspectiveCorrectionMatrix;
  else
    return projectionMatrix;
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
  params.x = tss;
}

