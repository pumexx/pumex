#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define MODEL_ID 1
#define MAX_BONES 511

const vec3 lightDirection = vec3(0,0,1);

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inTangent;
layout (location = 3) in vec3 inUV;
layout (location = 4) in vec4 inBoneWeight;
layout (location = 5) in vec4 inBoneIndex;

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

layout (binding = 0) uniform CameraUbo
{
  mat4 viewMatrix;
  mat4 viewMatrixInverse;
  mat4 projectionMatrix;
  vec4 observerPosition;
  vec4 params;
} camera;

layout (binding = 1) uniform PositionSbo
{
  mat4  position;
  mat4  bones[MAX_BONES];
} object;

layout (std430,binding = 2) readonly buffer MaterialTypesSbo
{
  MaterialTypeDefinition materialTypes[];
};

layout (std430,binding = 3) readonly buffer MaterialVariantsSbo
{
  MaterialVariantDefinition materialVariants[];
};

layout (location = 0) out vec3 outEcPosition;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec3 outTangent;
layout (location = 3) out vec3 outBitangent;
layout (location = 4) out vec2 outUV;
layout (location = 5) flat out uint materialID;

void main()
{
  mat4 boneTransform = object.bones[int(inBoneIndex[0])] * inBoneWeight[0];
  boneTransform     += object.bones[int(inBoneIndex[1])] * inBoneWeight[1];
  boneTransform     += object.bones[int(inBoneIndex[2])] * inBoneWeight[2];
  boneTransform     += object.bones[int(inBoneIndex[3])] * inBoneWeight[3];
  mat4 modelMatrix  = object.position * boneTransform;
  mat4 mvMatrix     = camera.viewMatrix * modelMatrix;
  vec4 ecPos4       = mvMatrix * vec4(inPos.xyz, 1.0);
  gl_Position       = camera.projectionMatrix * ecPos4;
  outEcPosition     = ecPos4.xyz / ecPos4.w;

  mat3 normalMatrix = mat3(inverse(transpose(mvMatrix)));
  outNormal         = normalMatrix * inNormal;
  outTangent        = normalMatrix * inTangent;
  outBitangent      = cross(outNormal, outTangent);
  outUV             = inUV.xy;

  materialID  = materialVariants[materialTypes[MODEL_ID].variantFirst + 0].materialFirst + uint(inUV.z);
}
