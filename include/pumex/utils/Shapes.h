//
// Copyright(c) 2017 Pawe³ Ksiê¿opolski ( pumexx )
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
#include <memory>
#include <pumex/Export.h>
#include <pumex/Asset.h>

namespace pumex
{
// collection of methods that create basic geometries
PUMEX_EXPORT void addBox(Geometry& geometry, float halfX, float halfY, float halfZ, bool drawFrontFace);
PUMEX_EXPORT void addBox(Geometry& geometry, glm::vec3 min, glm::vec3 max, bool drawFrontFace);

PUMEX_EXPORT void addSphere(Geometry& geometry, const glm::vec3& origin, float radius, uint32_t numSegments, uint32_t numRows, bool drawFrontFace);
PUMEX_EXPORT void addCone(Geometry& geometry, const glm::vec3& origin, float radius, float height, uint32_t numSegments, uint32_t numRows, bool createBottom);
PUMEX_EXPORT void addCylinder(Geometry& geometry, const glm::vec3& origin, float radius, float height, uint32_t numSegments, bool drawFrontFace, bool createBottom, bool createTop);
PUMEX_EXPORT void addCapsule(Geometry& geometry, const glm::vec3& origin, float radius, float cylinderHeight, uint32_t numSegments, uint32_t numRows, bool drawFrontFace, bool createBottom, bool createTop);
PUMEX_EXPORT void addQuad(Geometry& geometry, const glm::vec3& corner, const glm::vec3& widthVec, const glm::vec3& heightVec, float l = 0.0f, float b = 1.0f, float r = 1.0f, float t = 0.0f);

PUMEX_EXPORT void addCylinderBody(Geometry& geometry, const glm::vec3& origin, float radius, float height, uint32_t numSegments, bool drawFrontFace);
PUMEX_EXPORT void addHalfSphere(Geometry& geometry, const glm::vec3& origin, float radius, unsigned int numSegments, unsigned int numRows, bool top, bool drawFrontFace);

PUMEX_EXPORT std::shared_ptr<Asset> createSimpleAsset( const Geometry& geometry, const std::string rootName );
PUMEX_EXPORT std::shared_ptr<Asset> createFullScreenTriangle();

}