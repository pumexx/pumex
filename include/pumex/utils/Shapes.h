#pragma once
#include <pumex/Export.h>
#include <pumex/Asset.h>

namespace pumex
{
// collection of methods that create basic geometries
PUMEX_EXPORT void addBox(Geometry& geometry, float halfX, float halfY, float halfZ);
PUMEX_EXPORT void addBox(Geometry& geometry, glm::vec3 min, glm::vec3 max);

PUMEX_EXPORT void addSphere(Geometry& geometry, const glm::vec3& origin, float radius, uint32_t numSegments, uint32_t numRows, bool drawFrontFace);
PUMEX_EXPORT void addCone(Geometry& geometry, const glm::vec3& origin, float radius, float height, uint32_t numSegments, uint32_t numRows, bool createBottom);
PUMEX_EXPORT void addCylinder(Geometry& geometry, const glm::vec3& origin, float radius, float height, uint32_t numSegments, bool drawFrontFace, bool createBottom, bool createTop);
PUMEX_EXPORT void addCapsule(Geometry& geometry, const glm::vec3& origin, float radius, float cylinderHeight, uint32_t numSegments, uint32_t numRows, bool drawFrontFace, bool createBottom, bool createTop);
PUMEX_EXPORT void addQuad(Geometry& geometry, const glm::vec3& corner, const glm::vec3& widthVec, const glm::vec3& heightVec, float l = 0.0f, float b = 1.0f, float r = 1.0f, float t = 0.0f);

PUMEX_EXPORT void addCylinderBody(Geometry& geometry, const glm::vec3& origin, float radius, float height, uint32_t numSegments, bool drawFrontFace);
PUMEX_EXPORT void addHalfSphere(Geometry& geometry, const glm::vec3& origin, float radius, unsigned int numSegments, unsigned int numRows, bool top, bool drawFrontFace);

PUMEX_EXPORT Asset* createSimpleAsset( const Geometry& geometry, const std::string rootName );

}