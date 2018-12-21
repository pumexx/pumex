#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inTangent;
layout (location = 3) in vec3 inBitangent;

layout (binding = 1) uniform samplerCube environmentCubeMap;

layout (binding = 2) uniform PrefilteredEnvironmentParamsUbo
{
  vec4 value; // rougness, resolution 0, 0
} params;

layout (location = 0) out vec4 outFragColor;

const float PI              = 3.14159265359;
const uint SAMPLE_COUNT     = 256u;
const float MEDIUMP_FLT_MAX = 65504.0;

float distributionGGX(float NdotH, float roughness)
{
  float a      = roughness*roughness;
  float a2     = a*a;
  float NdotH2 = NdotH*NdotH;
  float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
  denom        = PI * denom * denom;
  return a2 / denom;
}

// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
// efficient VanDerCorpus calculation.
float radicalInverseVdC(uint bits) 
{
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 hammersley(uint i, uint N)
{
  return vec2(float(i)/float(N), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, mat3 TBN, float roughness)
{
  float a = roughness*roughness;

  float phi = 2.0 * PI * Xi.x;
  float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
  float sinTheta = sqrt(1.0 - cosTheta*cosTheta);

  // from spherical coordinates to cartesian coordinates - halfway vector
  vec3 H;
  H.x = cos(phi) * sinTheta;
  H.y = sin(phi) * sinTheta;
  H.z = cosTheta;
  
  return normalize(TBN*H);
}

void main()
{
  float roughness = params.value.x;
  float resolution = params.value.y;

  vec3 N = normalize(inNormal);
  mat3 TBN = mat3(normalize(inTangent),normalize(inBitangent),N);
    
  // make the simplyfying assumption that V equals R equals the normal 
  vec3 R = N;
  vec3 V = N;

  vec3 prefilteredColor = vec3(0.0);
  float totalWeight = 0.0;

  for(uint i = 0u; i < SAMPLE_COUNT; ++i)
  {
    // generates a sample vector that's biased towards the preferred alignment direction (importance sampling).
    vec2 Xi = hammersley(i, SAMPLE_COUNT);
    vec3 H  = importanceSampleGGX(Xi, TBN, roughness);
    vec3 L  = normalize(2.0 * dot(V, H) * H - V);

    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    if(NdotL > 0.0)
    {
      // sample from the environment's mip level based on roughness/pdf
      float NdotH = max(dot(N, H), 0.0);
      float D     = distributionGGX(NdotH, roughness);
      float HdotV = max(dot(H, V), 0.0);
      float pdf = D * NdotH / (4.0 * HdotV + 1e-5);

      float saTexel  = 4.0 * PI / (6.0 * resolution * resolution);
      float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);

      float mipLevel = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel); 
            
      prefilteredColor += textureLod(environmentCubeMap, L, mipLevel).rgb * NdotL;
      totalWeight      += NdotL;
    }
  }
  prefilteredColor = prefilteredColor / totalWeight;

  outFragColor = vec4(prefilteredColor, 1.0);
}
