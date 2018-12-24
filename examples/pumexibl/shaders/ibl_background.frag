#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;

layout (binding = 1) uniform samplerCube environmentCube;

layout (location = 0) out vec4 outFragColor;

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
    vec3 normPos  = normalize(inPos);
    vec3 color    = texture(environmentCube, normPos).rgb;
//  color         = toneMappingACESFitted(color);
    color         = toneMappingACESFilm(color);
//  color         = toneMappingReinhard(color);
    color         = pow(color, vec3(1.0/2.2)); 
    outFragColor  = vec4(color, 1.0);
}
