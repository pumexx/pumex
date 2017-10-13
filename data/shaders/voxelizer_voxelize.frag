#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

const uint CLIPMAP_TEXTURE_COUNT = 4; // should be the same as in voxelizer.cpp

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inPosition;
layout (location = 4) in vec2 inPositionProjected;
layout (location = 5) flat in vec4 inAABB;

layout(binding = 2, RGBA8) uniform image3D voxelTexture[CLIPMAP_TEXTURE_COUNT];

void main() 
{
  if( any( lessThan(vec4(inPositionProjected,inAABB.zw), vec4(inAABB.xy,inPositionProjected )) ) )
    discard;
//above code is the same as below code, but faster ( it is checking if point is inside AABB )
//  if( inPositionProjected.x < inAABB.x || inPositionProjected.y < inAABB.y || inPositionProjected.x > inAABB.z || inPositionProjected.y > inAABB.w )
//    discard;

  vec4 color = vec4(inColor,1);

  vec3 address3D = inPosition * 0.5 + vec3(0.5);
  imageStore(voxelTexture[0], ivec3(imageSize(voxelTexture[0]) * address3D), color);
}