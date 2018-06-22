#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

//layout (binding = 0) uniform texture2DArray color;
layout (binding = 0) uniform texture2DArray color;
layout (binding = 1) uniform sampler samp;

layout (location = 0) in vec3 inUV;

layout (location = 0) out vec4 outColor;

void main() 
{
	float alpha = 0.3;

	vec2 p1 = vec2(2.0 * inUV - 1.0);
	vec2 p2 = p1 / (1.0 - alpha * length(p1));
	p2 = (p2 + 1.0) * 0.5;

	bool inside = ((p2.x >= 0.0) && (p2.x <= 1.0) && (p2.y >= 0.0 ) && (p2.y <= 1.0));
	outColor = inside ? texture(sampler2DArray(color,samp), inUV) : vec4(0.0);
}