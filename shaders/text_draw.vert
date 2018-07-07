#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 inPos;   // left, top, right, bottom
layout (location = 1) in vec4 inUV;    // left, top, right, bottom
layout (location = 2) in vec4 inColor; // font color

layout (location = 0) out vec4 outUV;
layout (location = 1) out vec4 outColor;

void main() 
{
  gl_Position = inPos;
  outUV       = inUV;
  outColor    = inColor;
}
