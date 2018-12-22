#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;

layout (location = 0) out vec3 outPos;

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
  outPos       = inPos; 
  mat4 rotView = mat4(mat3(camera.viewMatrix));
  vec4 clipPos = camera.projectionMatrix * rotView * vec4(inPos, 1.0);
  gl_Position  = clipPos.xyww;
}
