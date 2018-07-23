#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// change value below if a number of TextureSemantic types in MaterialSet.h has changed
#define TextureSemanticCount 11

layout (location = 0) in vec2 inUV;
layout (location = 1) flat in uint materialID;

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

void main() 
{
  vec4 color = texture( diffuseSamplers[ materialData[materialID].diffuseTextureIndex ], inUV );
  if(color.a<0.5)
    discard;
}