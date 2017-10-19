#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

const uint CLIPMAP_TEXTURE_COUNT = 4; // should be the same as in voxelizer.cpp

layout (location = 0) in vec3 inRayEnd;
layout (location = 1) in vec3 inEyePosition;

//layout (binding = 2) uniform sampler3D voxelTexture;
layout(binding = 2, RGBA8) uniform image3D voxelTexture[CLIPMAP_TEXTURE_COUNT];

layout (location = 0) out vec4 outFragColor;

void main() 
{
  // first we must calculate ray parameters ( direction, starting position )
  vec3 rayDir    = normalize(inRayEnd - inEyePosition);
  vec3 invRayDir = 1.0 / rayDir;
  vec3 t1        = (vec3(0)-inEyePosition)*invRayDir;
  vec3 t2        = (vec3(1)-inEyePosition)*invRayDir;
  vec3 tmin1     = min(t1,t2);
  vec3 tmax1     = max(t1,t2);
  float tmin     = max(0, max(tmin1.x, max(tmin1.y,tmin1.z)));
  float tmax     = min(tmax1.x, min(tmax1.y,tmax1.z));
  vec3 rayStart  = inEyePosition + tmin * rayDir;

//  outFragColor   = vec4(tmin,tmin,tmin,1);

  float tstep = 1.0 / imageSize(voxelTexture[0]).x;
  for(float t=0; t<tmax-tmin; t+=tstep)
  {
    ivec3 texCoord = ivec3(0,imageSize(voxelTexture[0]).y-1,0) + ivec3(1,-1,1)*ivec3(imageSize(voxelTexture[0]) * (rayStart + t * rayDir));
//    ivec3 texCoord = ivec3(imageSize(voxelTexture[0]) * (rayStart + t * rayDir));
    vec4 color = imageLoad(voxelTexture[0], texCoord);

    if(color.a>0.0)
    {
      outFragColor   = vec4(color.rgb,1);
      return;
    }
  }
  discard;

}