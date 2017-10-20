#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

const uint CLIPMAP_TEXTURE_COUNT = 4; // should be the same as in voxelizer.cpp
const vec3 normalDirections[3][2] = { vec3[2](vec3(-1,0,0), vec3(1,0,0)),   
                                      vec3[2](vec3(0,-1,0), vec3(0,1,0)),   
                                      vec3[2](vec3(0,0,-1), vec3(0,0,1)) };

layout (location = 0) in vec3 inRayEnd;
layout (location = 1) in vec3 inCubePosition;
layout (location = 2) in vec3 inLightVec;

layout(binding = 2, RGBA8) uniform image3D voxelTexture[CLIPMAP_TEXTURE_COUNT];

layout (location = 0) out vec4 outFragColor;

void main() 
{
  // calculate ray parameters : direction and starting position
  vec3 rayDir    = normalize(inRayEnd - inCubePosition);
  vec3 invRayDir = 1.0 / rayDir;
  vec3 t1        = (vec3(0)-inCubePosition)*invRayDir;
  vec3 t2        = (vec3(1)-inCubePosition)*invRayDir;
  vec3 tmin1     = min(t1,t2);
  vec3 tmax1     = max(t1,t2);
  float tmin     = max(0, max(tmin1.x, max(tmin1.y,tmin1.z)));
  float tmax     = min(tmax1.x, min(tmax1.y,tmax1.z));
  vec3 rayStart  = inCubePosition + tmin * rayDir;

  vec3 voxelTextureSize = imageSize(voxelTexture[0]);
  vec3 rayDirInTex = rayDir * voxelTextureSize;
  vec3 invRayDirInTex = 1.0 / rayDirInTex;

  float t=0;
  while( t < tmax-tmin )
  {
    vec3 texCoordInTex = voxelTextureSize * (rayStart + t * rayDir);
    vec4 color         = imageLoad(voxelTexture[0], ivec3(texCoordInTex));

    if(color.a>0.0)
    {
      // first we must calculate a normal - we are searching for a wall in a reverse direction
      ivec3 rdIndex = ivec3(step(0, -rayDirInTex));
      vec3 deltas   = (step(0, -rayDirInTex) - fract(texCoordInTex)) * invRayDirInTex;
      uint dimIndex =  (deltas.y>deltas.x) ? ( (deltas.z>deltas.y) ? 2 : 1 ) : ( (deltas.z>deltas.x) ? 2 : 0 );

      vec3 N        = normalDirections[dimIndex][rdIndex[dimIndex]];
      vec3 L        = normalize(inLightVec);
      vec3 R        = reflect(-L, N);
      vec3 ambient  = vec3(0.1,0.1,0.1);
      vec3 diffuse  = max(dot(N, L), 0.0) * vec3(0.9,0.9,0.9);
      outFragColor  = vec4(ambient + diffuse * color.rgb, 1.0);
      // FIXME : z-value still not outputted
      return;
    }
    vec3 deltas = (step(0, rayDirInTex) - fract(texCoordInTex)) * invRayDirInTex;
    t += max(min(deltas.x,min(deltas.y,deltas.z)), 0.0001) ;
  }
  discard;
}