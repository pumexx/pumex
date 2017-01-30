#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inUV;
layout (location = 3) in vec4 inBoneWeight;
layout (location = 4) in vec4 inBoneIndex;

struct InstanceData
{
  mat4  position;
  uint  typeID;
  uint  materialVariant;
  float brightness;
  float wavingAmplitude; 
  float wavingFrequency;
  float wavingOffset;
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
  mat4  viewMatrix;
  mat4  viewMatrixInverse;
  mat4  projectionMatrix;
  vec4  observerPosition;
  float currentTime;
} camera;

layout (std430,binding = 1) readonly buffer InstanceDataSbo
{
	InstanceData instances[ ];
};

layout (std430,binding = 2) readonly buffer OffValuesSbo
{
	uint typeOffsetValues[];
};

layout (std430,binding = 3) readonly buffer MaterialTypesSbo
{
	MaterialTypeDefinition materialTypes[];
};

layout (std430,binding = 4) readonly buffer MaterialVariantsSbo
{
	MaterialVariantDefinition materialVariants[];
};

const vec3 lightDirection = vec3(0,0,1);
const vec2 windDirection = vec2( 0.707, 0.707 );

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
	uint instanceIndex = typeOffsetValues[gl_InstanceIndex];
	mat4 modelMatrix   = instances[instanceIndex].position;

    float wavingAmplitute = max(0.0,inPos.z * instances[instanceIndex].wavingAmplitude);
	vec2 windTranslation = windDirection * wavingAmplitute * sin( instances[instanceIndex].wavingFrequency * camera.currentTime + instances[instanceIndex].wavingOffset );

	vec4 modelPosition = modelMatrix * vec4(inPos.xyz, 1.0);
	modelPosition.xy   += windTranslation;
	gl_Position        = camera.projectionMatrix * camera.viewMatrix * modelPosition;
	outNormal          = mat3(inverse(transpose(modelMatrix))) * inNormal;
	outColor           = vec3(1.0,1.0,1.0) * instances[instanceIndex].brightness ;
	outUV              = inUV.xy;
	
    vec4 pos           = camera.viewMatrix * modelPosition;
    outLightVec        = normalize ( mat3( camera.viewMatrixInverse ) * lightDirection );
    outViewVec         = -pos.xyz;

	materialID  = materialVariants[materialTypes[instances[instanceIndex].typeID].variantFirst + instances[instanceIndex].materialVariant].materialFirst + uint(inUV.z);
}