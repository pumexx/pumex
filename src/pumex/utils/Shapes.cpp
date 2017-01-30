#include <pumex/utils/Shapes.h>

namespace pumex
{

void addBox(Geometry& geometry, float halfX, float halfY, float halfZ)
{
  addBox(geometry, glm::vec3(-halfX, -halfY, -halfZ), glm::vec3(halfX, halfY, halfZ));
}

void addBox(Geometry& geometry, glm::vec3 a, glm::vec3 b)
{
  VertexAccumulator acc(geometry.semantic);

  glm::vec3 m[2];
  m[0] = a;
  m[1] = b;
  if (m[0].x > m[1].x) std::swap(m[0].x, m[1].x);
  if (m[0].y > m[1].y) std::swap(m[0].y, m[1].y);
  if (m[0].z > m[1].z) std::swap(m[0].z, m[1].z);

  static uint32_t v[] = { 0,0,0,   1,0,0,   0,0,1,   1,0,1,  
                          1,0,0,   1,1,0,   1,0,1,   1,1,1,
                          1,1,0,   0,1,0,   1,1,1,   0,1,1,
                          0,1,0,   0,0,0,   0,1,1,   0,0,1,
                          0,1,0,   1,1,0,   0,0,0,   1,0,0,
                          0,0,1,   1,0,1,   0,1,1,   1,1,1 };
  static float n[] = { 0,-1,0,  1,0,0,  0,1,0,  -1,0,0,  0,0,-1,  0,0,1 };
  static float t[] = { 0,0,  1,0,  0,1,  1,1 };
  uint32_t verticesSoFar = geometry.getVertexCount();

  // vertices
  for (uint32_t wall = 0; wall < 6; ++wall)
  {
    for (uint32_t i = 0; i < 12; i+=3)
    {
      acc.set(VertexSemantic::Position, m[v[12 * wall + i + 0]].x, m[v[12 * wall + i + 1]].y, m[v[12 * wall + i + 2]].z);
      acc.set(VertexSemantic::Normal, n[3 * wall + 0], n[3 * wall + 1], n[3 * wall + 2]);
      acc.set(VertexSemantic::TexCoord, t[2 * i / 3 + 0], t[2 * i / 3 + 1]);
      geometry.pushVertex(acc);
    }
  }

  //indices
  for (uint32_t wall = 0; wall < 6; ++wall)
  {
    geometry.indices.push_back(verticesSoFar + wall * 4 + 0);
    geometry.indices.push_back(verticesSoFar + wall * 4 + 1);
    geometry.indices.push_back(verticesSoFar + wall * 4 + 2);

    geometry.indices.push_back(verticesSoFar + wall * 4 + 2);
    geometry.indices.push_back(verticesSoFar + wall * 4 + 1);
    geometry.indices.push_back(verticesSoFar + wall * 4 + 3);
  }
}

void addSphere(Geometry& geometry, const glm::vec3& origin, float radius, uint32_t numSegments, uint32_t numRows, bool drawFrontFace)
{
  addHalfSphere(geometry, origin, radius, numSegments, numRows, false, drawFrontFace);

  addHalfSphere(geometry, origin, radius, numSegments, numRows, true, drawFrontFace);
}

void addCone(Geometry& geometry, const glm::vec3& origin, float radius, float height, uint32_t numSegments, uint32_t numRows, bool createBottom)
{
  VertexAccumulator acc(geometry.semantic);

  float normalz     = radius / (sqrtf(radius*radius + height*height));
  float normalRatio = 1.0f / (sqrtf(1.0f + normalz*normalz));
  normalz           *= normalRatio;

  float angleDelta        = 2.0f* fpi / (float)numSegments;
  float texCoordHorzDelta = 1.0 / (float)numSegments;
  float texCoordRowDelta  = 1.0 / (float)numRows;
  float hDelta            = height / (float)numRows;
  float rDelta            = radius / (float)numRows;

  float baseOffset = 0.0f;
  float topz       = height + baseOffset;
  float topr       = 0.0f;
  float topv       = 1.0f;
  float basez      = topz - hDelta;
  float baser      = rDelta;
  float basev      = topv - texCoordRowDelta;

  uint32_t verticesSoFar;
  for (uint32_t rowi = 0; rowi<numRows; ++rowi, topz = basez, basez -= hDelta, topr = baser, baser += rDelta, topv = basev, basev -= texCoordRowDelta)
  {
    verticesSoFar = geometry.getVertexCount();
    float angle = 0.0f;
    float texCoord = 0.0f;
    for (uint32_t topi = 0; topi<numSegments; ++topi, angle += angleDelta, texCoord += texCoordHorzDelta) 
    {
      float c = cosf(angle);
      float s = sinf(angle);

      acc.set(VertexSemantic::Position, c*topr + origin.x, s*topr + origin.y, topz + origin.z);
      acc.set(VertexSemantic::Normal, c*normalRatio, s*normalRatio, normalz);
      acc.set(VertexSemantic::TexCoord, texCoord, topv);
      geometry.pushVertex(acc);

      acc.set(VertexSemantic::Position, c*baser + origin.x, s*baser + origin.y, basez + origin.z);
      acc.set(VertexSemantic::Normal, c*normalRatio, s*normalRatio, normalz);
      acc.set(VertexSemantic::TexCoord, texCoord, basev);
      geometry.pushVertex(acc);
    }

    acc.set(VertexSemantic::Position, topr + origin.x, 0.0f + origin.y, topz + origin.z);
    acc.set(VertexSemantic::Normal, normalRatio, 0.0f, normalz);
    acc.set(VertexSemantic::TexCoord, 1.0f, topv);
    geometry.pushVertex(acc);

    acc.set(VertexSemantic::Position, baser + origin.x, 0.0f + origin.y, basez + origin.z);
    acc.set(VertexSemantic::Normal, normalRatio, 0.0f, normalz);
    acc.set(VertexSemantic::TexCoord, 1.0f, basev);
    geometry.pushVertex(acc);

    for (uint32_t topi = 0; topi < numSegments; ++topi)
    {
      geometry.indices.push_back(verticesSoFar + 2 * topi);
      geometry.indices.push_back(verticesSoFar + 2 * topi + 1);
      geometry.indices.push_back(verticesSoFar + 2 * topi + 3);

      geometry.indices.push_back(verticesSoFar + 2 * topi + 3);
      geometry.indices.push_back(verticesSoFar + 2 * topi + 2);
      geometry.indices.push_back(verticesSoFar + 2 * topi);
    }
  }

  if (createBottom)
  {
    verticesSoFar  = geometry.getVertexCount();
    float angle    = 0.0f;
    float texCoord = 1.0f;
    basez          = baseOffset;

    acc.set(VertexSemantic::Position, 0.0f + origin.x, 0.0f + origin.y, basez + origin.z);
    acc.set(VertexSemantic::Normal, 0.0f, 0.0f, -1.0f);
    acc.set(VertexSemantic::TexCoord, 0.5f, 0.5f);
    geometry.pushVertex(acc);

    for (uint32_t topi = 0; topi<numSegments; ++topi, angle += angleDelta, texCoord += texCoordHorzDelta)
    {
      float c = cosf(angle);
      float s = sinf(angle);

      acc.set(VertexSemantic::Position, c*radius + origin.x, s*radius + origin.y, basez + origin.z);
      acc.set(VertexSemantic::Normal, 0.0f, 0.0f, -1.0f);
      acc.set(VertexSemantic::TexCoord, texCoord, basev);
      geometry.pushVertex(acc);
    }

    acc.set(VertexSemantic::Position, radius + origin.x, 0.0f + origin.y, basez + origin.z);
    acc.set(VertexSemantic::Normal, 0.0f, 0.0f, -1.0f);
    acc.set(VertexSemantic::TexCoord, texCoord, basev);
    geometry.pushVertex(acc);

    for (uint32_t topi = 0; topi <= numSegments; ++topi)
    {
      geometry.indices.push_back(verticesSoFar);
      geometry.indices.push_back(verticesSoFar + topi + 1);
      geometry.indices.push_back(verticesSoFar + topi);
    }
  }
}

void addCylinder(Geometry& geometry, const glm::vec3& origin, float radius, float height, uint32_t numSegments, bool drawFrontFace, bool createBottom, bool createTop)
{
  VertexAccumulator acc(geometry.semantic);

  const float angleDelta = 2.0f * fpi / (float)numSegments;
  const float texCoordDelta = 1.0f / (float)numSegments;

  addCylinderBody(geometry, origin, radius,height, numSegments,drawFrontFace);

  if (createBottom)
  {
    uint32_t verticesSoFar = geometry.getVertexCount();

    float angle = 0.0f;
    float texCoord = 1.0f;
    float basez = -0.5*height;

    acc.set(VertexSemantic::Position, 0.0f + origin.x, 0.0f + origin.y, basez + origin.z);
    if (drawFrontFace)
      acc.set(VertexSemantic::Normal, 0.0f, 0.0f, -1.0f );
    else
      acc.set(VertexSemantic::Normal, 0.0f, 0.0f, 1.0f);
    acc.set(VertexSemantic::TexCoord, 0.5f, 0.5f );
    geometry.pushVertex(acc);

    for (uint32_t topi = 0; topi<numSegments; ++topi, angle += angleDelta, texCoord += texCoordDelta)
    {
      float c = cosf(angle);
      float s = sinf(angle);

      acc.set(VertexSemantic::Position, c*radius + origin.x, s*radius + origin.y, basez + origin.z);
      if (drawFrontFace)
        acc.set(VertexSemantic::Normal, 0.0f, 0.0f, -1.0f );
      else
        acc.set(VertexSemantic::Normal, 0.0f, 0.0f, 1.0f);
      acc.set(VertexSemantic::TexCoord, texCoord, 1.0f );
      geometry.pushVertex(acc);
    }

    acc.set(VertexSemantic::Position, radius + origin.x, 0.0f + origin.y, basez + origin.z);
    if (drawFrontFace)
      acc.set(VertexSemantic::Normal, 0.0f, 0.0f, -1.0f );
    else
      acc.set(VertexSemantic::Normal, 0.0f, 0.0f, 1.0f);
    acc.set(VertexSemantic::TexCoord, texCoord, 1.0f );
    geometry.pushVertex(acc);

    for (uint32_t topi = 0; topi <= numSegments; ++topi)
    {
      if (drawFrontFace)
      {
        geometry.indices.push_back(verticesSoFar);
        geometry.indices.push_back(verticesSoFar + topi + 1);
        geometry.indices.push_back(verticesSoFar + topi);
      }
      else
      {
        geometry.indices.push_back(verticesSoFar);
        geometry.indices.push_back(verticesSoFar + topi);
        geometry.indices.push_back(verticesSoFar + topi + 1);
      }
    }
  }

  if (createTop)
  {
    uint32_t verticesSoFar = geometry.getVertexCount();

    float angle = 0.0f;
    float texCoord = 1.0f;
    float basez = 0.5*height;

    acc.set(VertexSemantic::Position, 0.0f + origin.x, 0.0f + origin.y, basez + origin.z);
    if (drawFrontFace)
      acc.set(VertexSemantic::Normal, 0.0f, 0.0f, 1.0f );
    else
      acc.set(VertexSemantic::Normal, 0.0f, 0.0f, -1.0f);
    acc.set(VertexSemantic::TexCoord, 0.5f, 0.5f );
    geometry.pushVertex(acc);

    for (uint32_t topi = 0; topi<numSegments; ++topi, angle += angleDelta, texCoord += texCoordDelta)
    {
      float c = cosf(angle);
      float s = sinf(angle);

      acc.set(VertexSemantic::Position, c*radius + origin.x, s*radius + origin.y, basez + origin.z);
      if (drawFrontFace)
        acc.set(VertexSemantic::Normal, 0.0f, 0.0f, 1.0f);
      else
        acc.set(VertexSemantic::Normal, 0.0f, 0.0f, -1.0f);
      acc.set(VertexSemantic::TexCoord, texCoord, 1.0f );
      geometry.pushVertex(acc);
    }

    acc.set(VertexSemantic::Position, radius + origin.x, 0.0f + origin.y, basez + origin.z);
    if (drawFrontFace)
      acc.set(VertexSemantic::Normal, 0.0f, 0.0f, 1.0f);
    else
      acc.set(VertexSemantic::Normal, 0.0f, 0.0f, -1.0f);
    acc.set(VertexSemantic::TexCoord, texCoord, 1.0f);
    geometry.pushVertex(acc);

    for (uint32_t topi = 0; topi <= numSegments; ++topi)
    {
      if (drawFrontFace)
      {
        geometry.indices.push_back(verticesSoFar);
        geometry.indices.push_back(verticesSoFar + topi);
        geometry.indices.push_back(verticesSoFar + topi + 1);
      }
      else
      {
        geometry.indices.push_back(verticesSoFar);
        geometry.indices.push_back(verticesSoFar + topi + 1);
        geometry.indices.push_back(verticesSoFar + topi);
      }
    }
  }
}

void addCapsule(Geometry& geometry, const glm::vec3& origin, float radius, float cylinderHeight, uint32_t numSegments, uint32_t numRows, bool drawFrontFace, bool createBottom, bool createTop)
{
  // numRows must be even, so two halfSpeheres meet at the equator ( required for capsules )
  numRows &= ~1;

  addCylinderBody(geometry, origin, radius, cylinderHeight, numSegments, drawFrontFace);

  if (createBottom)
    addHalfSphere(geometry, origin - glm::vec3(0, 0, 0.5*cylinderHeight), radius, numSegments, numRows, false, drawFrontFace);

  if (createBottom)
    addHalfSphere(geometry, origin + glm::vec3(0, 0, 0.5*cylinderHeight), radius, numSegments, numRows, true, drawFrontFace);
}

void addQuad(Geometry& geometry, const glm::vec3& corner, const glm::vec3& widthVec, const glm::vec3& heightVec, float l, float b, float r, float t)
{
  VertexAccumulator acc(geometry.semantic);
  uint32_t verticesSoFar = geometry.getVertexCount();

  glm::vec3 c0 = corner + heightVec;
  glm::vec3 c1 = corner;
  glm::vec3 c2 = corner + widthVec;
  glm::vec3 c3 = corner + widthVec + heightVec;
  glm::vec3 n  = cross(widthVec,heightVec);

  acc.set(VertexSemantic::Position, c0.x, c0.y, c0.z);
  acc.set(VertexSemantic::Normal, n.x, n.y, n.z );
  acc.set(VertexSemantic::TexCoord, l, t );
  geometry.pushVertex(acc);

  acc.set(VertexSemantic::Position, c1.x, c1.y, c1.z);
  acc.set(VertexSemantic::Normal, n.x, n.y, n.z);
  acc.set(VertexSemantic::TexCoord, l, b);
  geometry.pushVertex(acc);

  acc.set(VertexSemantic::Position, c2.x, c2.y, c2.z);
  acc.set(VertexSemantic::Normal, n.x, n.y, n.z);
  acc.set(VertexSemantic::TexCoord, r, b);
  geometry.pushVertex(acc);

  acc.set(VertexSemantic::Position, c3.x, c3.y, c3.z);
  acc.set(VertexSemantic::Normal, n.x, n.y, n.z);
  acc.set(VertexSemantic::TexCoord, r, t);
  geometry.pushVertex(acc);

  geometry.indices.push_back(verticesSoFar + 0);
  geometry.indices.push_back(verticesSoFar + 1);
  geometry.indices.push_back(verticesSoFar + 2);

  geometry.indices.push_back(verticesSoFar + 2);
  geometry.indices.push_back(verticesSoFar + 3);
  geometry.indices.push_back(verticesSoFar + 0);
}

void addCylinderBody(Geometry& geometry, const glm::vec3& origin, float radius, float height, uint32_t numSegments, bool drawFrontFace)
{
  VertexAccumulator acc(geometry.semantic);

  const float angleDelta = 2.0f * fpi / (float)numSegments;
  const float texCoordDelta = 1.0f / (float)numSegments;

  const float r = radius;
  const float h = height;

  float basez = -h*0.5f;
  float topz = h*0.5f;

  float angle = 0.0f;
  float texCoord = 0.0f;

  uint32_t verticesSoFar = geometry.getVertexCount();

  for (uint32_t bodyi = 0; bodyi<numSegments; ++bodyi, angle += angleDelta, texCoord += texCoordDelta)
  {
    float c = cosf(angle);
    float s = sinf(angle);

    acc.set(VertexSemantic::Position, c*radius + origin.x, s*radius + origin.y, topz + origin.z);
    if (drawFrontFace)
      acc.set(VertexSemantic::Normal, c, s, 0.0f);
    else
      acc.set(VertexSemantic::Normal, -c, -s, 0.0f);
    acc.set(VertexSemantic::TexCoord, texCoord, 1.0f);
    geometry.pushVertex(acc);

    acc.set(VertexSemantic::Position, c*radius + origin.x, s*radius + origin.y, basez + origin.z);
    if (drawFrontFace)
      acc.set(VertexSemantic::Normal, c, s, 0.0f);
    else
      acc.set(VertexSemantic::Normal, -c, -s, 0.0f);
    acc.set(VertexSemantic::TexCoord, texCoord, 0.0f);
    geometry.pushVertex(acc);
  }

  acc.set(VertexSemantic::Position, radius + origin.x, 0.0f + origin.y, topz + origin.z);
  if (drawFrontFace)
    acc.set(VertexSemantic::Normal, 1.0f, 0.0f, 0.0f);
  else
    acc.set(VertexSemantic::Normal, -1.0f, 0.0f, 0.0f);
  acc.set(VertexSemantic::TexCoord, 1.0f, 1.0f);
  geometry.pushVertex(acc);

  acc.set(VertexSemantic::Position, radius + origin.x, 0.0f + origin.y, basez + origin.z);
  if (drawFrontFace)
    acc.set(VertexSemantic::Normal, 1.0f, 0.0f, 0.0f);
  else
    acc.set(VertexSemantic::Normal, -1.0f, 0.0f, 0.0f);
  acc.set(VertexSemantic::TexCoord, 1.0f, 0.0f);
  geometry.pushVertex(acc);

  for (uint32_t bodyi = 0; bodyi < numSegments; ++bodyi)
  {
    geometry.indices.push_back(verticesSoFar + 2 * bodyi);
    if (drawFrontFace)
    {
      geometry.indices.push_back(verticesSoFar + 2 * bodyi + 1);
      geometry.indices.push_back(verticesSoFar + 2 * bodyi + 3);
    }
    else
    {
      geometry.indices.push_back(verticesSoFar + 2 * bodyi + 3);
      geometry.indices.push_back(verticesSoFar + 2 * bodyi + 1);
    }

    geometry.indices.push_back(verticesSoFar + 2 * bodyi + 3);
    if (drawFrontFace)
    {
      geometry.indices.push_back(verticesSoFar + 2 * bodyi + 2);
      geometry.indices.push_back(verticesSoFar + 2 * bodyi);
    }
    else
    {
      geometry.indices.push_back(verticesSoFar + 2 * bodyi);
      geometry.indices.push_back(verticesSoFar + 2 * bodyi + 2);
    }
  }
}

void addHalfSphere(Geometry& geometry, const glm::vec3& origin, float radius, unsigned int numSegments, unsigned int numRows, bool top, bool drawFrontFace)
{
  VertexAccumulator acc(geometry.semantic);

  float lDelta = fpi / (float)numRows;
  float vDelta = 1.0f / (float)numRows;

  float angleDelta = fpi*2.0f / (float)numSegments;
  float texCoordHorzDelta = 1.0f / (float)numSegments;

  float lBase = -fpi*0.5f + (top ? (lDelta*(numRows / 2)) : 0.0f);
  float rBase = (top ? (cosf(lBase)*radius) : 0.0f);
  float zBase = (top ? (sinf(lBase)*radius) : -radius);
  float vBase = (top ? (vDelta*(numRows / 2)) : 0.0f);
  float nzBase = (top ? (sinf(lBase)) : -1.0f);
  float nRatioBase = (top ? (cosf(lBase)) : 0.0f);

  unsigned int rowbegin = top ? numRows / 2 : 0;
  unsigned int rowend = top ? numRows : numRows / 2;

  uint32_t verticesSoFar = 0;
  for (unsigned int rowi = rowbegin; rowi<rowend; ++rowi)
  {
    verticesSoFar = geometry.getVertexCount();
    float lTop = lBase + lDelta;
    float rTop = cosf(lTop)*radius;
    float zTop = sinf(lTop)*radius;
    float vTop = vBase + vDelta;
    float nzTop = sinf(lTop);
    float nRatioTop = cosf(lTop);

    float angle = 0.0f;
    float texCoord = 0.0f;

    for (unsigned int topi = 0; topi<numSegments; ++topi, angle += angleDelta, texCoord += texCoordHorzDelta)
    {
      float c = cosf(angle);
      float s = sinf(angle);

      acc.set(VertexSemantic::Position, c*rTop + origin.x, s*rTop + origin.y, zTop + origin.z);
      if (drawFrontFace)
        acc.set(VertexSemantic::Normal, c*nRatioTop, s*nRatioTop, nzTop);
      else
        acc.set(VertexSemantic::Normal, -c*nRatioTop, -s*nRatioTop, -nzTop);
      acc.set(VertexSemantic::TexCoord, texCoord, vTop);
      geometry.pushVertex(acc);

      acc.set(VertexSemantic::Position, c*rBase + origin.x, s*rBase + origin.y, zBase + origin.z);

      if (drawFrontFace)
        acc.set(VertexSemantic::Normal, c*nRatioBase, s*nRatioBase, nzBase);
      else
        acc.set(VertexSemantic::Normal, -c*nRatioBase, -s*nRatioBase, -nzBase);
      acc.set(VertexSemantic::TexCoord, texCoord, vBase);
      geometry.pushVertex(acc);
    }

    acc.set(VertexSemantic::Position, rTop + origin.x, 0.0f + origin.y, zTop + origin.z);
    if (drawFrontFace)
      acc.set(VertexSemantic::Normal, nRatioTop, 0.0f, nzTop);
    else
      acc.set(VertexSemantic::Normal, -nRatioTop, 0.0f, -nzTop);
    acc.set(VertexSemantic::TexCoord, 1.0f, vTop);
    geometry.pushVertex(acc);

    acc.set(VertexSemantic::Position, rBase + origin.x, 0.0f + origin.y, zBase + origin.z);
    if (drawFrontFace)
      acc.set(VertexSemantic::Normal, nRatioBase, 0.0f, nzBase);
    else
      acc.set(VertexSemantic::Normal, -nRatioBase, 0.0f, -nzBase);
    acc.set(VertexSemantic::TexCoord, 1.0f, vBase);
    geometry.pushVertex(acc);

    for (uint32_t topi = 0; topi < numSegments; ++topi)
    {
      geometry.indices.push_back(verticesSoFar + 2 * topi);
      if (drawFrontFace)
      {
        geometry.indices.push_back(verticesSoFar + 2 * topi + 1);
        geometry.indices.push_back(verticesSoFar + 2 * topi + 3);
      }
      else
      {
        geometry.indices.push_back(verticesSoFar + 2 * topi + 3);
        geometry.indices.push_back(verticesSoFar + 2 * topi + 1);
      }

      geometry.indices.push_back(verticesSoFar + 2 * topi + 3);
      if (drawFrontFace)
      {
        geometry.indices.push_back(verticesSoFar + 2 * topi + 2);
        geometry.indices.push_back(verticesSoFar + 2 * topi);
      }
      else
      {
        geometry.indices.push_back(verticesSoFar + 2 * topi);
        geometry.indices.push_back(verticesSoFar + 2 * topi + 2);
      }
    }

    lBase = lTop;
    rBase = rTop;
    zBase = zTop;
    vBase = vTop;
    nzBase = nzTop;
    nRatioBase = nRatioTop;
  }
}

Asset* createSimpleAsset(const Geometry& geometry, const std::string rootName)
{
  Asset* result = new Asset;
  result->geometries.push_back(geometry);

  Skeleton::Bone bone;
  result->skeleton.bones.emplace_back(bone);
  result->skeleton.boneNames.push_back(rootName);
  result->skeleton.invBoneNames.insert({ rootName, 0 });

  // this asset has material not defined
  return result;
}

}