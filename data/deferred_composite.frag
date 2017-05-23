#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(input_attachment_index = 2, binding = 0) uniform subpassInputMS inPosition;
layout(input_attachment_index = 3, binding = 1) uniform subpassInputMS inNormal;
layout(input_attachment_index = 4, binding = 2) uniform subpassInputMS inFragColor;

layout (location = 0) out vec4 outColor;

void main() 
{
  outColor = subpassLoad(inFragColor,gl_SampleID);
}