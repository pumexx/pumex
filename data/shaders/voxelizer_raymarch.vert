#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define MAX_BONES 511

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inBoneWeight;
layout (location = 4) in vec4 inBoneIndex;

layout (binding = 0) uniform CameraUbo
{
  mat4 viewMatrix;
  mat4 viewMatrixInverse;
  mat4 projectionMatrix;
  vec4 observerPosition;
} camera;

layout (binding = 1) uniform PositionSbo
{
  mat4  position;
  mat4  bones[MAX_BONES];
} object;

layout (location = 0) out vec3 outRayEnd;
layout (location = 1) out vec3 outEyePosition;

void main() 
{
  // mesh that is used on this shader is a box with dimensions (0,0,0)..(1,1,1) and its faces are looking inside
  // it does not use any bones
  mat4 modelMatrix = object.position;
  outRayEnd        = inPos.xyz;
  vec4 eyePosition = inverse(camera.viewMatrix * modelMatrix) * vec4(0,0,0,1);
  outEyePosition   = eyePosition.xyz / eyePosition.w;
  gl_Position      = camera.projectionMatrix * camera.viewMatrix * modelMatrix * vec4(inPos.xyz, 1.0);
}
