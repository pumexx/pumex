#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec4 inColor;

layout (binding = 1) uniform sampler2D fontTexture;

layout (location = 0) out vec4 outFragColor;

void main() 
{
  float alpha  = texture( fontTexture, inUV ).r;
  outFragColor = vec4( inColor.xyz, alpha );
}