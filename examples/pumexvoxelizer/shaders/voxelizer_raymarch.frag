#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define MAX_BONES 511

const vec3 normalDirections[3][2] = { vec3[2](vec3(-1,0,0), vec3(1,0,0)),   
                                      vec3[2](vec3(0,-1,0), vec3(0,1,0)),   
                                      vec3[2](vec3(0,0,-1), vec3(0,0,1)) };

layout (location = 0) in vec3 inVolumeRayEnd;                // ray end in volume coordinates
layout (location = 1) in vec3 inLightVec;
layout (location = 2) flat in vec3 inVolumeEyePosition;     // eye position in volume coordinates
layout (location = 3) flat in vec3 inVolumeNearPlaneStart;  // near plane start in volume coordinates
layout (location = 4) flat in vec3 inVolumeNearPlaneNormal; // near plane normal in volume coordinates

layout (binding = 0) uniform CameraUbo
{
  mat4 viewMatrix;
  mat4 viewMatrixInverse;
  mat4 projectionMatrix;
  vec4 observerPosition;
  vec4 params;
} camera;

layout (binding = 1) uniform PositionSbo
{
  mat4  position;
  mat4  bones[MAX_BONES];
} object;

layout(binding = 2, RGBA8) uniform image3D voxelTexture;

layout (location = 0) out vec4 outFragColor;

void main() 
{
  // calculate ray parameters : direction and starting position
  vec3 rayDir    = normalize(inVolumeRayEnd - inVolumeEyePosition);
  vec3 invRayDir = 1.0 / rayDir;

  // calculate intersection between ray and near plane
  float tnear          = -dot(inVolumeEyePosition-inVolumeNearPlaneStart,inVolumeNearPlaneNormal) / dot(rayDir,inVolumeNearPlaneNormal);
  vec3 volumeNearPlane = inVolumeEyePosition + tnear * rayDir;

  // find closest intersection with a (0,0,0)..(1,1,1) volume
  vec3 t1        = (vec3(0)-volumeNearPlane)*invRayDir;
  vec3 t2        = (vec3(1)-volumeNearPlane)*invRayDir;
  vec3 tmin1     = min(t1,t2);
  vec3 tmax1     = max(t1,t2);
  float tmin     = max(0.0, max(tmin1.x, max(tmin1.y,tmin1.z)));
  float tmax     = min(tmax1.x, min(tmax1.y,tmax1.z));
  vec3 rayStart  = volumeNearPlane + tmin * rayDir;

  vec3 voxelTextureSize = imageSize(voxelTexture);
  vec3 rayDirInTex      = rayDir * voxelTextureSize;
  vec3 invRayDirInTex   = 1.0 / rayDirInTex;

  float t=0.0;
  if(tmin==0.0)
  {
    vec3 texCoordInTex = voxelTextureSize * (rayStart + t * rayDir);
    vec3 deltas = (step(0, rayDirInTex) - fract(texCoordInTex)) * invRayDirInTex;
    t += max(min(deltas.x,min(deltas.y,deltas.z)), 0.00001) ;
  }
  while( t < tmax-tmin )
  {
    vec3 texCoordInTex = voxelTextureSize * (rayStart + t * rayDir);
    vec4 color         = imageLoad(voxelTexture, ivec3(texCoordInTex));

    if(color.a>0.0)
    {
      // we know that texture coordinates are taken from one of the voxel walls. But which one ?
      vec3 dtc        = abs(vec3(0.5) - fract(texCoordInTex));
      uint dimIndex   = (dtc.y>dtc.x) ?  ( (dtc.z>dtc.y) ? 2 : 1 ) : ( (dtc.z>dtc.x) ? 2 : 0 ) ;
      ivec3 normalIdx = ivec3(step(0, -rayDirInTex));

      vec3 N        = normalDirections[dimIndex][normalIdx[dimIndex]];
      vec3 L        = normalize(inLightVec);
      vec3 R        = reflect(-L, N);
      vec3 ambient  = vec3(0.1,0.1,0.1);
      vec3 diffuse  = max(dot(N, L), 0.0) * vec3(0.9,0.9,0.9);
      outFragColor  = vec4(ambient + diffuse * color.rgb, 1.0);
	  
      vec4 eyePos   = camera.projectionMatrix * camera.viewMatrix * object.position * vec4(rayStart + t*rayDir, 1);
      gl_FragDepth  = eyePos.z/eyePos.w;
      return;
    }
    vec3 deltas = (step(0, rayDirInTex) - fract(texCoordInTex)) * invRayDirInTex;
    t           += max(min(deltas.x,min(deltas.y,deltas.z)), 0.0001) ;
  }
  discard;
}
