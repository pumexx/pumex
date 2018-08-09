#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inPosition;
layout (location = 4) flat in vec3 inMinAABB;
layout (location = 5) flat in vec3 inMaxAABB;

layout(binding = 2, RGBA8) uniform image3D voxelTexture;

void main()
{
  if( any( lessThan(inPosition,inMinAABB) ) || any(lessThan(inMaxAABB,inPosition) ) )
    discard;

  vec4 color     = vec4(inColor,1);
  vec3 address3D = inPosition * 0.5 + vec3(0.5);
  imageStore(voxelTexture, ivec3(imageSize(voxelTexture) * address3D), color);
}
