#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 inPosUV; // position=(x,y), uv=(z,w)
layout (location = 1) in vec4 inColor; // font color

layout (binding = 0) uniform CameraUbo
{
  mat4  viewMatrix;
  mat4  viewMatrixInverse;
  mat4  projectionMatrix;
  vec4  observerPosition;
  float currentTime;
} camera;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec4 outColor;

void main()
{
  gl_Position = camera.projectionMatrix * vec4( inPosUV.xy, 0, 1 ) ;
  outUV       = inPosUV.zw;
  outColor    = inColor;
}
