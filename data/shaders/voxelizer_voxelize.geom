#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

const uint CLIPMAP_TEXTURE_COUNT = 4; // should be the same as in voxelizer.cpp

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;

layout (location = 0) in vec3 inNormal[];
layout (location = 1) in vec3 inColor[];
layout (location = 2) in vec2 inUV[];
layout (location = 3) in vec4 inPosition[];

layout(binding = 2, RGBA8) uniform image3D voxelTexture[CLIPMAP_TEXTURE_COUNT];

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec4 outPosition;
layout (location = 4) out vec2 outPositionProjected;
layout (location = 5) flat out vec4 outAABB;

void main()
{
  // calculating on which plane this triangle will be projected
  vec3 tn = abs( cross( inPosition[1].xyz-inPosition[0].xyz, inPosition[2].xyz-inPosition[0].xyz) );
  // which value is maximum ? x=0, y=1, z=2
  uint maxIndex = (tn.y>tn.x) ? ( (tn.z>tn.y) ? 2 : 1 ) : ( (tn.z>tn.x) ? 2 : 0 );

  // calculating pixel size
  vec3 imSize         = imageSize(voxelTexture[0]);
  vec2 pixelSize      = vec2( 1.0/imSize.x, 1.0/imSize.y ) ;
  float pixelDiagonal = 1.414213562373095 * pixelSize.x; // sure ?

  vec3 edges[3];
  vec3 edgeNormals[3];
  vec4 finalPosition[3];
  outAABB = vec4(2.0,2.0,-2.0,-2.0);
  // project triangle on xy, yz or yz plane where it's visible most
  // also - calculate data for conservative rasterization
  for(uint i=0; i<3; ++i)
  {
    switch(maxIndex)
    {
    case 0:
      finalPosition[i] = vec4(inPosition[i].yz, 0, 1);
      edges[i]         = vec3(inPosition[(i+1)%3].yz - inPosition[i].yz, 0 ); 
      break;
    case 1: 
      finalPosition[i] = vec4(inPosition[i].xz, 0, 1);
      edges[i]         = vec3(inPosition[(i+1)%3].xz - inPosition[i].xz, 0 ); 
      break;
    case 2: 
      finalPosition[i] = vec4(inPosition[i].xy, 0, 1);
      edges[i]         = vec3(inPosition[(i+1)%3].xy - inPosition[i].xy, 0 ); 
      break;
    }
    edgeNormals[i] = cross(edges[i],vec3(0,0,1));

    outAABB.xy = min( outAABB.xy, finalPosition[i].xy );
    outAABB.zw = max( outAABB.zw, finalPosition[i].xy );
  }
  outAABB.xy -= pixelSize;
  outAABB.zw += pixelSize;

  for(uint i=0; i<3; ++i)
  {
    outNormal            = inNormal[i];
    outColor             = inColor[i];
    outUV                = inUV[i];
    outPosition          = inPosition[i];
    outPositionProjected = finalPosition[i].xy; // FIXME ? - output values do not seem to take dilated triangle into account...
    gl_Position          = finalPosition[i];
    // calculate new xy position ( conservative rasterization )
    gl_Position.xy       = gl_Position.xy + 3*pixelDiagonal * ( (edges[(i+2)%3].xy/dot(edges[(i+2)%3].xy, edgeNormals[i].xy)) + (edges[i].xy/dot(edges[i].xy,edgeNormals[(i+2)%3].xy)) );
    EmitVertex();
  }
  EndPrimitive();
}