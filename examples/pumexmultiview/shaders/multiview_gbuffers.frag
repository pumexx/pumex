#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inTangent;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inPosition;
layout (location = 4) flat in uint materialID;

struct MaterialData
{
  uint  diffuseTextureIndex;
  uint  roughnessTextureIndex;
  uint  metallicTextureIndex;
  uint  normalTextureIndex;
};

layout (std430,binding = 4) readonly buffer MaterialDataSbo
{
  MaterialData materialData[];
};

layout (binding = 5) uniform sampler2D diffuseSamplers[64];
layout (binding = 6) uniform sampler2D roughnessSamplers[64];
layout (binding = 7) uniform sampler2D metallicSamplers[64];
layout (binding = 8) uniform sampler2D normalSamplers[64];

layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec4 outAlbedo;
layout (location = 3) out vec3 outRoughnessMetallic;

void main()
{
  vec4 color = texture( diffuseSamplers[ materialData[materialID].diffuseTextureIndex ], inUV );
  if(color.a<0.5)
    discard;
  color.rgb = pow( color.rgb, vec3(2.2));
  outAlbedo = color;

  outRoughnessMetallic = vec3
  (
    texture( roughnessSamplers[ materialData[materialID].roughnessTextureIndex ], inUV ).r,
    texture( metallicSamplers[ materialData[materialID].metallicTextureIndex ], inUV ).r,
    0.0
  );

  vec3 N    = normalize(inNormal);
  vec3 T    = normalize(inTangent);
  vec3 B    = cross(T, N);
  mat3 TBN  = mat3(T, B, N);
  outNormal = texture( normalSamplers[ materialData[materialID].normalTextureIndex ], inUV ).xyz;
  outNormal = outNormal * 2.0 - vec3(1.0);
  outNormal = TBN * normalize(outNormal);

  outPosition     = inPosition;
}
