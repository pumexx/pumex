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
  mat4  position;
  mat4  bones[MAX_BONES];
} object;

layout (location = 0) out vec3 outVolumeRayEnd;              // ray end in volume coordinates
layout (location = 1) out vec3 outLightVec;
layout (location = 2) flat out vec3 outVolumeEyePosition;    // eye position in volume coordinates
layout (location = 3) flat out vec3 outVolumeNearPlaneStart; // near plane start in volume coordinates
layout (location = 4) flat out vec3 outVolumeNearPlaneNormal;// near plane normal in volume coordinates

void main()
{
  // Mesh that is used in this shader is a box with dimensions (0,0,0)..(1,1,1) and its faces are looking inside. It does not use any bones
  mat4 modelMatrix           = object.position;
  outVolumeRayEnd            = inPos.xyz;
  outLightVec                = normalize ( mat3( camera.viewMatrixInverse ) * lightDirection );
  vec4 volumeEyePosition     = inverse(camera.viewMatrix * modelMatrix) * vec4(0,0,0,1);
  outVolumeEyePosition       = volumeEyePosition.xyz / volumeEyePosition.w;
  vec4 volumeNearPlaneStart  = inverse(camera.projectionMatrix * camera.viewMatrix * modelMatrix) * vec4(0,0,0,1);
  outVolumeNearPlaneStart    = volumeNearPlaneStart.xyz / volumeNearPlaneStart.w;
  vec4 volumeNearPlaneNormal = normalize(inverse(camera.projectionMatrix * camera.viewMatrix * modelMatrix) * vec4(0,0,1,1));
  outVolumeNearPlaneNormal   = volumeNearPlaneNormal.xyz / volumeNearPlaneNormal.w;

  gl_Position                = camera.projectionMatrix * camera.viewMatrix * modelMatrix * vec4(inPos.xyz, 1.0);
}
