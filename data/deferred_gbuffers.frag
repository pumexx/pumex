#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// change value below if a number of TextureSemantic types in MaterialSet.h has changed
#define TextureSemanticCount 11

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inViewVec;
layout (location = 4) in vec3 inLightVec;
layout (location = 5) flat in uint materialID;

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

layout (location = 0) out vec4 outFragColor;

void main() 
{
	vec4 color = texture( textureSamplers[ textureSamplerOffsets[0] + materialData[materialID].diffuseTextureIndex ], inUV );
//	vec4 color = texture( textureSamplers[ textureSamplerOffsets[1] + materialData[materialID].specularTextureIndex ], inUV );
//	vec4 color = texture( textureSamplers[ textureSamplerOffsets[2] + materialData[materialID].normalTextureIndex ], inUV );
	if(color.a<0.5)
	  discard;

	vec3 N        = normalize(inNormal);
	vec3 L        = normalize(inLightVec);
	vec3 V        = normalize(inViewVec);
	vec3 R        = reflect(-L, N);
	vec3 ambient  = vec3(0.1,0.1,0.1);
	vec3 diffuse  = max(dot(N, L), 0.0) * vec3(0.9,0.9,0.9);
	vec3 specular = pow(max(dot(R, V), 0.0), 128.0) * vec3(1,1,1);
	outFragColor  = vec4(ambient + diffuse * color.rgb + specular, 1.0);
}