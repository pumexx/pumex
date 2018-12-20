#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;

layout (binding = 1) uniform sampler2D equirectangularMap;

layout (location = 0) out vec4 outFragColor;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphericalMap(vec3 v)
{
  vec2 uv = vec2(atan(-v.y, v.x), asin(v.z));
  uv *= invAtan;
  uv += 0.5;
  return uv;
}

void main()
{		
  vec2 uv = sampleSphericalMap(normalize(inPos));
  vec3 color = texture(equirectangularMap, uv).rgb;
  outFragColor = vec4(color, 1.0);
}