#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define MAX_BONES 511

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inBoneWeight;
layout (location = 4) in vec4 inBoneIndex;

layout (binding = 0) uniform CameraUbo
{
  mat4 viewMatrix;        // during voxelization this matrix is set to identity
  mat4 viewMatrixInverse; // during voxelization this matrix is set to identity
  mat4 projectionMatrix;  // this matrix is set to orthogonal matrix 
  vec4 observerPosition;
} camera;

layout (binding = 1) uniform PositionSbo
{
  mat4  position;
  mat4  bones[MAX_BONES];
} object;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec4 outPosition;

void main() 
{
  mat4 boneTransform = object.bones[int(inBoneIndex[0])] * inBoneWeight[0];
  boneTransform     += object.bones[int(inBoneIndex[1])] * inBoneWeight[1];
  boneTransform     += object.bones[int(inBoneIndex[2])] * inBoneWeight[2];
  boneTransform     += object.bones[int(inBoneIndex[3])] * inBoneWeight[3];	
  mat4 modelMatrix  = object.position * boneTransform;

  outNormal   = mat3(inverse(transpose(modelMatrix))) * inNormal;
  outColor    = vec3(1.0,1.0,0.0);
  outUV       = inUV;
  // camera.viewMatrix is omitted because it's identity matrix
  outPosition = camera.projectionMatrix * modelMatrix * vec4(inPos.xyz, 1.0);
	
  gl_Position = outPosition;
}
