#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inUV;

layout (location = 0) out vec3 outUV;

void main() 
{
  gl_Position = vec4(inPos,1.0);
  outUV = inUV;
}
