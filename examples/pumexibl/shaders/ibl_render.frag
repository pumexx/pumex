#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inEcPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inTangent;
layout (location = 3) in vec3 inBitangent;
layout (location = 4) in vec2 inUV;

layout (binding = 0) uniform CameraUbo
{
  mat4 viewMatrix;
  mat4 viewMatrixInverse;
  mat4 projectionMatrix;
  vec4 observerPosition;
  vec4 params;
} camera;

layout (binding = 3) uniform samplerCube irradianceMap;
layout (binding = 4) uniform samplerCube prefilteredEnvironmentMap;
layout (binding = 5) uniform sampler2D   brdfMap;

layout (location = 0) out vec4 outFragColor;

const float PI                 = 3.14159265359;
const vec4  worldLightPosition = vec4(0.0, -20.0, 0.0, 1.0);
const vec4  lightColor         = vec4(1.0, 1.0, 1.0, 1.0)*300;
const float lightRadius        = 100.0;

float distributionGGX(float NdotH, float roughness)
{
  float a      = roughness*roughness;
  float a2     = a*a;
  float NdotH2 = NdotH*NdotH;
  float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
  denom        = PI * denom * denom;
  return a2 / denom;
}

float geometrySchlickGGX(float NdotX, float roughness)
{
  float r     = (roughness + 1.0);
  float k     = (r*r) / 8.0;

  float nom   = NdotX;
  float denom = NdotX * (1.0 - k) + k;

  return nom / denom;
}

float geometrySmith(float NdotL, float NdotV, float roughness)
{
  float ggxL  = geometrySchlickGGX(NdotL, roughness);
  float ggxV  = geometrySchlickGGX(NdotV, roughness);
  return ggxL * ggxV;
}

vec3 fresnelSchlick(float LdotH, vec3 F0)
{
  return F0 + (1.0 - F0) * pow(1.0 - LdotH, 5.0);
}

vec3 fresnelSchlickRoughness(float NdotV, vec3 F0, float roughness)
{
  return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
}

// Tone mapping code adapted from 
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// The code in this file was originally written by Stephen Hill (@self_shadow), who deserves all
// credit for coming up with this fit and implementing it. Buy him a beer next time you see him. :)

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACESInputMat = mat3(
  vec3(0.59719, 0.35458, 0.04823),
  vec3(0.07600, 0.90834, 0.01566),
  vec3(0.02840, 0.13383, 0.83777)
);

// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat = mat3(
  vec3( 1.60475, -0.53108, -0.07367),
  vec3(-0.10208,  1.10813, -0.00605),
  vec3(-0.00327, -0.07276,  1.07602)
);

vec3 RRTAndODTFit(vec3 acesColor)
{
  vec3 a = acesColor * (acesColor + 0.0245786f) - 0.000090537f;
  vec3 b = acesColor * (0.983729f * acesColor + 0.4329510f) + 0.238081f;
  return a / b;
}

vec3 toneMappingACESFitted(vec3 rgbColor)
{
  vec3 result = ACESOutputMat * RRTAndODTFit( ACESInputMat * rgbColor );
  return clamp( result, 0.0, 1.0);
}

// tone mapping adapted from https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 toneMappingACESFilm( vec3 rgbColor )
{
  float a = 2.51f;
  float b = 0.03f;
  float c = 2.43f;
  float d = 0.59f;
  float e = 0.14f;
  return clamp((rgbColor*(a*rgbColor+b))/(rgbColor*(c*rgbColor+d)+e), 0.0, 1.0);
}

// Reinhard tone mapping using luminance to scale the result
vec3 toneMappingReinhard(vec3 rgbColor)
{
  float luminance  = dot(vec3(0.2126, 0.7152, 0.0722), rgbColor);
  float tonemapped = luminance / ( luminance+1.0 );
  float tscale     = tonemapped / luminance;
  return clamp( rgbColor * tscale, 0.0, 1.0 );
}

// naive Reinhard tone mapping made in RGB space, often used in many demos/examples across the internet
vec3 toneMappingReinhardNaive(vec3 rgbColor)
{
  return rgbColor / ( rgbColor + vec3(1.0) );
}



void main()
{
  vec3  albedo           = vec3( 0.2, 0.2, 0.8 );// pow(texture(albedoMap, texCoord).rgb, vec3(2.2)).rgb;
  float metallic         = 0.0; //texture(metallicMap, texCoord).r;
  float roughness        = 0.8; //texture(roughnessMap, texCoord).r;
  
  mat3  TBN              = mat3(normalize(inTangent),normalize(inBitangent),normalize(inNormal));
  vec3  texNormal        = vec3(0.5, 0.5, 1.0);   //texture(normalMap, texCoord).rgb;
  texNormal              = normalize(texNormal * 2.0 - vec3(1.0));   
  vec3  N                = TBN * texNormal;
  
  vec3  V                = normalize( -inEcPosition  );
  float NdotV            = clamp(dot(N, V), 1e-5, 1.0);  

  vec3  F0               = mix(vec3(0.04), albedo, metallic);
  
  // point light - begin

  vec3  finalColor       = vec3(0.0);
  
  vec4  ecLightPosition4 = camera.viewMatrix * worldLightPosition;
  vec3  ecLightPosition  = ecLightPosition4.xyz / ecLightPosition4.w;
  
  vec3  L                = normalize(ecLightPosition - inEcPosition);
  float NdotL            = clamp(dot(N, L), 1e-5, 1.0);
  vec3  H                = normalize(V + L);
  float LdotH            = clamp(dot(L, H), 0.0, 1.0);
  float NdotH            = clamp(dot(N, H), 0.0, 1.0);
  float lightDistance    = length(ecLightPosition - inEcPosition);
  
  // Unreal Engine lighting model with defined light radius
  float distRadius       = lightDistance / lightRadius;
  distRadius             = distRadius * distRadius;
  float fallofNominator  = clamp(1.0-distRadius*distRadius, 0.0, 1.0);
  float attenuation      = fallofNominator*fallofNominator / (lightDistance*lightDistance+1.0);
  
  // physically based lighting model with squared distance attenuation
//  float attenuation      = 1.0 / (lightDistance*lightDistance);

  vec3  radiance         = lightColor.xyz * attenuation;

  // Cook-Torrance BRDF
  // values of roughness less than 14e-3 cause GGX to go south
  float D                = distributionGGX(NdotH, max(14e-3,roughness) );
  float G                = geometrySmith(NdotL, NdotV, roughness);
  vec3  F                = fresnelSchlick(LdotH, F0);
  
  vec3  nominator        = D * G * F;
  float denominator      = 4 * NdotV * NdotL;
  vec3  specular         = nominator / denominator;
  
  vec3  kS               = F;
  vec3  kD               = vec3(1.0) - kS;
  kD                     *= 1.0 - metallic;	 

  // scale light by NdotL and add to outgoing radiance
  finalColor             += (kD * albedo / PI + specular) * radiance * NdotL;

  // point light - end

  // IBL
  mat3  viewInverse3     = mat3(camera.viewMatrixInverse);
  vec3  kS_ibl           = fresnelSchlickRoughness(NdotV, F0, roughness); 
  vec3  kD_ibl           = vec3(1.0) - kS_ibl;
  kD_ibl                *= 1.0 - metallic; 
  vec3  irradiance       = texture(irradianceMap, viewInverse3*N ).rgb;
  vec3  diffuse          = irradiance * albedo;

  vec3  R                = viewInverse3*reflect(-V, N);
  vec3  prefilteredColor = textureLod(prefilteredEnvironmentMap, R,  roughness * textureQueryLevels(prefilteredEnvironmentMap)).rgb;
  vec2  brdf             = texture(brdfMap, vec2(NdotV, roughness)).rg;
  vec3  specular_ibl     = prefilteredColor * (kS_ibl * brdf.x + brdf.y);
  vec3  ambient          = (kD_ibl * diffuse + specular_ibl);// * ao;  

  finalColor             = finalColor + ambient;
//  finalColor             = ambient;

  // tone mapping
  //finalColor             = toneMappingACESFitted(finalColor);
  finalColor             = toneMappingACESFilm(finalColor);
  //finalColor             = toneMappingReinhard(finalColor);
  
  // gamma correction
  finalColor             = pow(finalColor, vec3(1.0/2.2));
}
