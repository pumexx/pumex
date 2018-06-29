#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inUV;
layout (location = 3) in vec4 inBoneWeight;
layout (location = 4) in vec4 inBoneIndex;

#define MAX_BONES 9

struct DynamicInstanceData
{
  uvec4 id;     // id, typeid, materialVariant, 0
  vec4  params; // brightness, 0, 0, 0
  mat4  position;
  mat4  bones[MAX_BONES];
};

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

layout (set = 0, binding = 0) uniform CameraUbo
{
  mat4 viewMatrix;
  mat4 viewMatrixInverse;
  mat4 projectionMatrix;
  vec4 observerPosition;
  vec4 params;
} camera;

layout (set = 0, binding = 1) readonly buffer DynamicInstanceDataSbo
{
  DynamicInstanceData instances[ ];
};

layout (set = 0, binding = 2) readonly buffer ResultsSbo
{
  uint resultValues[];
};

layout (set = 0, binding = 3) readonly buffer MaterialTypesSbo
{
  MaterialTypeDefinition materialTypes[];
};

layout (set = 0, binding = 4) readonly buffer MaterialVariantsSbo
{
  MaterialVariantDefinition materialVariants[];
};

const vec3 lightDirection = vec3(0,0,1);
const vec2 windDirection = vec2( 0.707, 0.707 );

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outViewVec;
layout (location = 4) out vec3 outLightVec;
layout (location = 5) flat out uint materialID;

void main() 
{
  uint instanceIndex = resultValues[gl_InstanceIndex];
  mat4 boneTransform = instances[instanceIndex].bones[int(inBoneIndex[0])] * inBoneWeight[0];
  boneTransform     += instances[instanceIndex].bones[int(inBoneIndex[1])] * inBoneWeight[1];
  boneTransform     += instances[instanceIndex].bones[int(inBoneIndex[2])] * inBoneWeight[2];
  boneTransform     += instances[instanceIndex].bones[int(inBoneIndex[3])] * inBoneWeight[3];	
  mat4 modelMatrix   = instances[instanceIndex].position * boneTransform;

  gl_Position = camera.projectionMatrix * camera.viewMatrix * modelMatrix * vec4(inPos.xyz, 1.0);
  outNormal   = mat3(inverse(transpose(modelMatrix))) * inNormal;
  outColor    = vec3(1.0,1.0,1.0) * instances[instanceIndex].params[0] ;
  outUV       = inUV.xy;
	
  vec4 pos    = camera.viewMatrix * modelMatrix * vec4(inPos.xyz, 1.0);
  outLightVec = normalize ( mat3( camera.viewMatrixInverse ) * lightDirection );
  outViewVec  = -pos.xyz;

  materialID  = materialVariants[materialTypes[instances[instanceIndex].id[1]].variantFirst + instances[instanceIndex].id[2]].materialFirst + uint(inUV.z);
}