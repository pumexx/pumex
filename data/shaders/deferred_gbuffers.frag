#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// change value below if a number of TextureSemantic types in MaterialSet.h has changed
#define TextureSemanticCount 11

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inTangent;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inPosition;
layout (location = 4) flat in uint materialID;

struct MaterialData
{
  vec4  ambient;
  vec4  diffuse;
  vec4  specular;
  float shininess;
  uint  diffuseTextureIndex;
  uint  specularTextureIndex;
  uint  normalTextureIndex;
};


layout (std430,binding = 4) readonly buffer MaterialDataSbo
{
	MaterialData materialData[];
};

layout (std430,binding = 5) readonly buffer TextureSamplerOffsets
{
	uint textureSamplerOffsets[TextureSemanticCount];
};

layout (binding = 6) uniform sampler2D textureSamplers[64];

layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec4 outFragColor;

void main() 
{
	vec4 color = texture( textureSamplers[ textureSamplerOffsets[0] + materialData[materialID].diffuseTextureIndex ], inUV );
	if(color.a<0.5)
	  discard;
    color.rgb = pow( color.rgb, vec3(2.2));
    color.a = texture( textureSamplers[ textureSamplerOffsets[1] + materialData[materialID].specularTextureIndex ], inUV ).r;
    outFragColor    = color;

    vec3 N        = normalize(inNormal);
    vec3 T        = normalize(inTangent);
    vec3 B        = cross(N, T);
    mat3 TBN      = mat3(T, B, N);
    outNormal.xyz = texture( textureSamplers[ textureSamplerOffsets[2] + materialData[materialID].normalTextureIndex ], inUV ).xyz;
    outNormal.xyz = outNormal.xyz * 2.0 - vec3(1.0);
    outNormal.xyz = TBN * normalize(outNormal.xyz);
    outNormal     = vec4(outNormal.xyz * 0.5 + 0.5, 0.0);

    outPosition     = inPosition;
}