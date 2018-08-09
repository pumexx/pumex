#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define NUM_LIGHTS 4

struct LightPoint
{
  vec4 position;
  vec4 color;
  vec4 attenuation;
};

layout (location = 0) in vec2 inUV;

layout (binding = 0) uniform CameraUbo
{
  mat4 viewMatrix;
  mat4 viewMatrixInverse;
  mat4 projectionMatrix;
  vec4 observerPosition;
  vec4 params;
} camera;

layout (std430,binding = 1) readonly buffer Lights
{
  LightPoint lights[];
};

layout(input_attachment_index = 0, binding = 2) uniform subpassInputMS inPosition;
layout(input_attachment_index = 1, binding = 3) uniform subpassInputMS inNormal;
layout(input_attachment_index = 2, binding = 4) uniform subpassInputMS inAlbedo;
layout(input_attachment_index = 3, binding = 5) uniform subpassInputMS inRoughnessMetallic;

layout (location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float distributionGGX(vec3 N, vec3 H, float roughness)
{
  float a = roughness*roughness;
  float a2 = a*a;
  float NdotH = max(dot(N, H), 0.0);
  float NdotH2 = NdotH*NdotH;

  float nom   = a2;
  float denom = (NdotH2 * (a2 - 1.0) + 1.0);
  denom = PI * denom * denom;

  return nom / denom;
}

float geometrySchlickGGX(float NdotV, float roughness)
{
  float r = (roughness + 1.0);
  float k = (r*r) / 8.0;

  float nom   = NdotV;
  float denom = NdotV * (1.0 - k) + k;

  return nom / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  float ggx2 = geometrySchlickGGX(NdotV, roughness);
  float ggx1 = geometrySchlickGGX(NdotL, roughness);

  return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
  return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

void main()
{
  vec3 worldPosition = subpassLoad(inPosition,gl_SampleID).xyz;
  vec3 worldNormal   = subpassLoad(inNormal,gl_SampleID).xyz;
  vec3 albedo        = subpassLoad(inAlbedo,gl_SampleID).rgb;
  vec2 roughMetal    = subpassLoad(inRoughnessMetallic, gl_SampleID).rg;
  float roughness    = roughMetal.r;
  float metallic     = roughMetal.g;

  vec3 viewPosition = camera.viewMatrixInverse[3].xyz / camera.viewMatrixInverse[3].w;
  vec3 viewDir      = normalize(viewPosition - worldPosition);

  vec3 F0 = vec3(0.04);
  F0 = mix(F0, albedo, metallic);

  vec3 finalColor = vec3(0.0);
  for(uint i=0; i<NUM_LIGHTS; i++)
  {
    // calculate per-light radiance
    vec3 lightDir       = normalize(lights[i].position.xyz - worldPosition);
    vec3 halfDir        = normalize(viewDir + lightDir);
    float lightDistance = length(lights[i].position.xyz - worldPosition);
    float attenuation   = 1.0 / (lightDistance*lightDistance*lights[i].attenuation.z);
    vec3 radiance       = lights[i].color.xyz * attenuation;

    // Cook-Torrance BRDF
    float NDF = distributionGGX(worldNormal, halfDir, roughness);
    float G   = geometrySmith(worldNormal, viewDir, lightDir, roughness);
    vec3  F   = fresnelSchlick(max(dot(halfDir, viewDir), 0.0), F0);

    vec3 nominator    = NDF * G * F;
    float denominator = 4 * max(dot(worldNormal, viewDir), 0.0) * max(dot(worldNormal, lightDir), 0.0) + 0.001;
    vec3 specular     = nominator / denominator;

    // kS is equal to Fresnel
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD      *= 1.0 - metallic;

    // scale light by NdotL
    float NdotL = max(dot(worldNormal, lightDir), 0.0);
    // add to outgoing radiance Lo
    finalColor += (kD * albedo / PI + specular) * radiance * NdotL;
  }

  // Reinhard tone mapping
  finalColor = finalColor / (finalColor + vec3(1.0));
  // gamma correction
  finalColor = pow(finalColor, vec3(1.0/2.2));
  //output
  outColor = vec4(finalColor,1.0);
}
