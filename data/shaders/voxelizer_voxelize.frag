#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

const uint CLIPMAP_TEXTURE_COUNT = 4; // should be the same as in voxelizer.cpp

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inPosition;

layout(binding = 2, RGBA8) uniform image3D voxelTexture[CLIPMAP_TEXTURE_COUNT];

void main() 
{
	vec4 color = vec4(inColor,1);

	vec3 address3D = inPosition * 0.5 + vec3(0.5);
	ivec3 dim = imageSize(voxelTexture[0]);
    imageStore(voxelTexture[0], ivec3(imageSize(voxelTexture[0]) * address3D), color);
}