#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define MAX_BONES 511

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inUV;
layout (location = 3) in float inBoneWeight;
layout (location = 4) in float inBoneIndex;

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

const vec3 lightDirection = vec3(0,0,1);

out gl_PerVertex 
{
	vec4 gl_Position;   
};

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outViewVec;
layout (location = 4) out vec3 outLightVec;
layout (location = 5) flat out uint materialID;

void main() 
{
	mat4 boneTransform = object.bones[int(inBoneIndex)] * inBoneWeight;
	mat4 vertexTranslation = object.position * boneTransform;

	gl_Position = camera.projectionMatrix * camera.viewMatrix * vertexTranslation * vec4(inPos.xyz, 1.0);
	outNormal   = mat3(inverse(transpose(vertexTranslation))) * inNormal;
	outColor    = vec3(1.0,1.0,1.0);
	outUV       = inUV.xy;
	
    vec4 pos    = camera.viewMatrix * vertexTranslation * vec4(inPos.xyz, 1.0);
    outLightVec = normalize ( mat3( camera.viewMatrixInverse ) * lightDirection );
    outViewVec  = -pos.xyz;

	materialID  = materialVariants[materialTypes[object.typeID].variantFirst + 0].materialFirst + uint(inUV.z);
}
