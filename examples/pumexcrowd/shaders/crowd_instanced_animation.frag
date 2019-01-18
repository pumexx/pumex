#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct MaterialTypeDefinition
{
  uint variantFirst;
  uint variantSize;
};

struct MaterialVariantDefinition
{
  uint materialFirst;
  uint materialSize;
};

layout (std430,binding = 4) readonly buffer MaterialTypesSbo
{
  MaterialTypeDefinition materialTypes[];
};

layout (std430,binding = 5) readonly buffer MaterialVariantsSbo
{
  MaterialVariantDefinition materialVariants[];
};

struct MaterialData
{
  vec4  ambient;
  vec4  diffuse;
  vec4  specular;
  float shininess;
  uint  diffuseTextureIndex;
};

layout (std430,binding = 6) readonly buffer MaterialDataSbo
{
  MaterialData materialData[];
};

layout (binding = 7) uniform sampler2DArray samplerColorMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inViewVec;
layout (location = 4) in vec3 inLightVec;
layout (location = 5) flat in uvec3 materialID;

layout (location = 0) out vec4 outFragColor;

void main()
{
  uint matID  = materialVariants[materialTypes[materialID.x].variantFirst + materialID.y].materialFirst + materialID.z;

  vec4 color = texture(samplerColorMap, vec3(inUV,float(materialData[matID].diffuseTextureIndex)));
  if(color.a<0.5)
    discard;

  vec3 N        = normalize(inNormal);
  vec3 L        = normalize(inLightVec);
  vec3 V        = normalize(inViewVec);
  vec3 R        = reflect(-L, N);
  vec3 ambient  = materialData[matID].ambient.xyz;
  vec3 diffuse  = max(dot(N, L), 0.0) * materialData[matID].diffuse.xyz;
  vec3 specular = pow(max(dot(R, V), 0.0), materialData[matID].shininess) * materialData[matID].specular.xyz;
  outFragColor  = vec4(ambient + diffuse * color.rgb + specular, 1.0);
}
