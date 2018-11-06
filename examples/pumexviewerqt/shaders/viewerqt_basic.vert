#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define MAX_BONES 511

const vec3 lightDirection = vec3(0,0,1);

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inBoneWeight;
layout (location = 4) in vec4 inBoneIndex;

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
  vec4  color;
  mat4  position;
  mat4  bones[MAX_BONES];
} object;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outViewVec;
layout (location = 4) out vec3 outLightVec;

void main()
{
  mat4 boneTransform = object.bones[int(inBoneIndex[0])] * inBoneWeight[0];
  boneTransform     += object.bones[int(inBoneIndex[1])] * inBoneWeight[1];
  boneTransform     += object.bones[int(inBoneIndex[2])] * inBoneWeight[2];
  boneTransform     += object.bones[int(inBoneIndex[3])] * inBoneWeight[3];
  mat4 modelMatrix  = object.position * boneTransform;

  outNormal        = mat3(inverse(transpose(modelMatrix))) * inNormal;
  outColor         = object.color.xyz;
  outUV            = inUV;
  vec4 eyePosition = camera.viewMatrix * modelMatrix * vec4(inPos.xyz, 1.0);
  outLightVec      = normalize ( mat3( camera.viewMatrixInverse ) * lightDirection );
  outViewVec       = -eyePosition.xyz;

  gl_Position      = camera.projectionMatrix * eyePosition;
}
