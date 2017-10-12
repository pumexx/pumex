#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;

layout (location = 0) in vec3 inNormal[];
layout (location = 1) in vec3 inColor[];
layout (location = 2) in vec2 inUV[];
layout (location = 3) in vec4 inPosition[];

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec4 outPosition;

void main()
{
  // calculating on which plane this triangle will be projected
  vec3 tn = abs( cross( inPosition[1].xyz-inPosition[0].xyz, inPosition[2].xyz-inPosition[0].xyz) );
  // which value is maximum ? x=0, y=1, z=2
  uint maxIndex = (tn.y>tn.x) ? ( (tn.z>tn.y) ? 2 : 1 ) : ( (tn.z>tn.x) ? 2 : 0 );

  for(uint i=0; i<3; ++i)
  {
    outNormal   = inNormal[i];
    outColor    = inColor[i];
    outUV       = inUV[i];
    outPosition = inPosition[i];
    switch(maxIndex)
    {
    case 0: gl_Position = vec4(outPosition.y, outPosition.z, 0, 1); break;
    case 1: gl_Position = vec4(outPosition.x, outPosition.z, 0, 1); break;
    case 2: gl_Position = vec4(outPosition.x, outPosition.y, 0, 1); break;
    }
    EmitVertex();
  }
  EndPrimitive();
}