#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0) uniform texture2DArray color;
layout (binding = 1) uniform sampler samp;

layout (location = 0) in vec3 inUV;

layout (location = 0) out vec4 outColor;

const float a = 0.2f;
const float b = 0.0f;
const float c = 0.0f;
const float d = 1 - (a + b + c);

void main()
{
  vec2 p1    = 2.0 * inUV.xy - vec2(1.0);
  float len0 = length(p1);
  float len1 = a * pow(len0,4) + b * pow(len0,3) + c * pow(len0,2) + d * len0;
  vec2 p2    = normalize(p1) * len1;
  p2         = (p2 + vec2(1.0)) * 0.5;
  if( any( lessThan(p2,vec2(0.0)) ) || any(lessThan(vec2(1.0),p2) ) )
    discard;
  outColor   = texture(sampler2DArray(color,samp), vec3(p2,inUV.z));
}
