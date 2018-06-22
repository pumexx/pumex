#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inUV;
layout (location = 3) in vec4 inBoneWeight;
layout (location = 4) in vec4 inBoneIndex;

struct StaticInstanceData
{
  uvec4 id;
  vec4  params;
  mat4  position;
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

layout (set = 0, binding = 1) readonly buffer ResultIndexSbo
{
	uint instanceIndices[];
};

layout (set = 0, binding = 2) readonly buffer InstanceDataSbo
{
	StaticInstanceData instances[];
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
	uint instanceIndex = instanceIndices[gl_InstanceIndex];
	mat4 modelMatrix   = instances[instanceIndex].position;

    float wavingAmplitute = max(0.0,inPos.z * instances[instanceIndex].params[1]);
	vec2 windTranslation = windDirection * wavingAmplitute * sin( instances[instanceIndex].params[2] * camera.params[0] + instances[instanceIndex].params[3] );

	vec4 modelPosition = modelMatrix * vec4(inPos.xyz, 1.0);
	modelPosition.xy   += windTranslation;
	gl_Position        = camera.projectionMatrix * camera.viewMatrix * modelPosition;
	outNormal          = mat3(inverse(transpose(modelMatrix))) * inNormal;
	outColor           = vec3(1.0,1.0,1.0) * instances[instanceIndex].params[0] ;
	outUV              = inUV.xy;
	
    vec4 pos           = camera.viewMatrix * modelPosition;
    outLightVec        = normalize ( mat3( camera.viewMatrixInverse ) * lightDirection );
    outViewVec         = -pos.xyz;

	materialID  = materialVariants[materialTypes[instances[instanceIndex].id.y].variantFirst + instances[instanceIndex].id.z].materialFirst + uint(inUV.z);
}