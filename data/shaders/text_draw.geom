#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

layout (location = 0) in vec4 inUV[];
layout (location = 1) in vec4 inColor[];

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
  gl_Position = camera.projectionMatrix * vec4( gl_in[0].gl_Position.xw, 0, 1 ) ;
  outUV       = inUV[0].xw;
  outColor    = inColor[0];
  EmitVertex();

  gl_Position = camera.projectionMatrix * vec4( gl_in[0].gl_Position.zw, 0, 1 ) ;
  outUV       = inUV[0].zw;
  outColor    = inColor[0];
  EmitVertex();

  gl_Position = camera.projectionMatrix * vec4( gl_in[0].gl_Position.xy, 0, 1 ) ;
  outUV       = inUV[0].xy;
  outColor    = inColor[0];
  EmitVertex();

  gl_Position = camera.projectionMatrix * vec4( gl_in[0].gl_Position.zy, 0, 1 ) ;
  outUV       = inUV[0].zy;
  outColor    = inColor[0];
  EmitVertex();

  EndPrimitive();
}