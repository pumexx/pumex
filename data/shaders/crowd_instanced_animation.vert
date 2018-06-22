#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define MAX_BONES 63

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inUV;
layout (location = 3) in vec4 inBoneWeight;
layout (location = 4) in vec4 inBoneIndex;

struct PositionData
{
  mat4  position;
  mat4  bones[MAX_BONES];
};

struct InstanceData
{
  uint positionIndex;
  uint typeID;
  uint materialVariant;
  uint mainInstance;
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

layout (binding = 0) uniform CameraUbo
{
  mat4 viewMatrix;
  mat4 viewMatrixInverse;
  mat4 projectionMatrix;
  vec4 observerPosition;
  vec4 params;
} camera;

layout (std430,binding = 1) readonly buffer PositionSbo
{
	PositionData positions[ ];
};

layout (std430,binding = 2) readonly buffer InstanceDataSbo
{
	InstanceData instances[ ];
};

layout (std430,binding = 3) readonly buffer OffValuesSbo
{
	uint typeOffsetValues[];
};

layout (std430,binding = 4) readonly buffer MaterialTypesSbo
{
	MaterialTypeDefinition materialTypes[];
};

layout (std430,binding = 5) readonly buffer MaterialVariantsSbo
{
	MaterialVariantDefinition materialVariants[];
};

const vec3 lightDirection = vec3(0,0,1);

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outViewVec;
layout (location = 4) out vec3 outLightVec;
layout (location = 5) flat out uint materialID;

void main() 
{
	uint instanceIndex = typeOffsetValues[gl_InstanceIndex];
    uint positionIndex = instances[instanceIndex].positionIndex;
	mat4 boneTransform = positions[positionIndex].bones[int(inBoneIndex[0])] * inBoneWeight[0];
	boneTransform     += positions[positionIndex].bones[int(inBoneIndex[1])] * inBoneWeight[1];
	boneTransform     += positions[positionIndex].bones[int(inBoneIndex[2])] * inBoneWeight[2];
	boneTransform     += positions[positionIndex].bones[int(inBoneIndex[3])] * inBoneWeight[3];	
	mat4 modelMatrix  = positions[positionIndex].position * boneTransform;

	gl_Position = camera.projectionMatrix * camera.viewMatrix * modelMatrix * vec4(inPos.xyz, 1.0);
	outNormal   = mat3(inverse(transpose(modelMatrix))) * inNormal;
	outColor    = vec3(1.0,1.0,1.0);
	outUV       = inUV.xy;
	
    vec4 pos    = camera.viewMatrix * modelMatrix * vec4(inPos.xyz, 1.0);
    outLightVec = normalize ( mat3( camera.viewMatrixInverse ) * lightDirection );
    outViewVec  = -pos.xyz;

	materialID  = materialVariants[materialTypes[instances[instanceIndex].typeID].variantFirst + instances[instanceIndex].materialVariant].materialFirst + uint(inUV.z);
}