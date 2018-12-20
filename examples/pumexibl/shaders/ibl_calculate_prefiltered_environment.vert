#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;

layout (location = 0) out vec3 outPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec3 outTangent;
layout (location = 3) out vec3 outBitangent;

const float EPSILON = 1e-6;

layout (binding = 0) uniform CameraUbo
{
  mat4 viewMatrix;
  mat4 viewMatrixInverse;
  mat4 projectionMatrix;
  vec4 observerPosition;
  vec4 params;
} camera;

void main()
{
  gl_Position  = camera.projectionMatrix * camera.viewMatrix * vec4(inPos,1.0);
  outPos       = inPos;
  outNormal    = normalize(inPos);
  vec3 perp    = cross( vec3(0,0,1), outNormal  );
  if(length(perp) < EPSILON)
    perp = cross( outNormal, vec3(1,0,0) );
  outBitangent = normalize(cross( outNormal, perp ));
  outTangent   = cross(outNormal,outBitangent);
}
