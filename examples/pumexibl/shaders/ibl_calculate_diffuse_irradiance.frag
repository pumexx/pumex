#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inTangent;
layout (location = 3) in vec3 inBitangent;

layout (binding = 1) uniform samplerCube environmentCubeMap;

layout (location = 0) out vec4 outFragColor;

const float PI = 3.14159265359;

void main()
{		
  vec3 N = normalize(inNormal);
  mat3 TBN = mat3(normalize(inTangent),normalize(inBitangent),N);
  
  vec3 irradiance  = vec3(0.0);
  float sampleDelta = 0.025;
  float nrSamples = 0.0; 
  for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
  {
    for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
    {
      // spherical to cartesian (in tangent space)
      vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
      // tangent space to world
      vec3 sampleVec = TBN * tangentSample; 

      irradiance += texture(environmentCubeMap, sampleVec).rgb * cos(theta) * sin(theta);
      nrSamples++;
    }
  }
  irradiance = PI * irradiance * (1.0 / float(nrSamples));	
  
  outFragColor = vec4(irradiance, 1.0);
}
