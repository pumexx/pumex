#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inUV;
layout (location = 3) in float inBoneWeight;
layout (location = 4) in float inBoneIndex;

out gl_PerVertex 
{
	vec4 gl_Position;   
};

void main() 
{
  gl_Position = vec4(inPos,1.0);
}
