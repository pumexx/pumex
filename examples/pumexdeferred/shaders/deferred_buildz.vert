#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define MAX_BONES 255

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inTangent;
layout (location = 3) in vec3 inUV;
layout (location = 4) in float inBoneWeight;
layout (location = 5) in float inBoneIndex;

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
  uint  typeID;
} object;

layout (std430,binding = 2) readonly buffer MaterialTypesSbo
{
  MaterialTypeDefinition materialTypes[];
};

layout (std430,binding = 3) readonly buffer MaterialVariantsSbo
{
  MaterialVariantDefinition materialVariants[];
};

layout (location = 0) out vec2 outUV;
layout (location = 1) flat out uint materialID;

void main()
{
  mat4 boneTransform = object.bones[int(inBoneIndex)] * inBoneWeight;
  mat4 modelMatrix   = object.position * boneTransform;
  vec4 outPosition   = modelMatrix * vec4(inPos.xyz, 1.0);
  gl_Position        = camera.projectionMatrix * camera.viewMatrix * outPosition;

  outUV              = inUV.xy;
  materialID         = materialVariants[materialTypes[object.typeID].variantFirst + 0].materialFirst + uint(inUV.z);
}
