#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

const float PI          = 3.14159265359;
const uint SAMPLE_COUNT = 1024u;

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

float geometrySchlickGGX_IBL(float NdotV, float roughness)
{
  float a     = roughness;
  float k     = (a * a) / 2.0;

  float nom   = NdotV;
  float denom = NdotV * (1.0 - k) + k;

  return nom / denom;
}

float geometrySmith_IBL(float NdotL, float NdotV, float roughness)
{
  float ggxL  = geometrySchlickGGX_IBL(NdotL, roughness);
  float ggxV  = geometrySchlickGGX_IBL(NdotV, roughness);
  return ggxL * ggxV;
}

vec2 integrateBRDF(float NdotV, float roughness)
{
  vec3 V;
  V.x = sqrt(1.0 - NdotV*NdotV);
  V.y = 0.0;
  V.z = NdotV;

  float A = 0.0;
  float B = 0.0; 

  mat3 TBN = mat3( vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0) );
    
  for(uint i = 0u; i < SAMPLE_COUNT; ++i)
  {
    // generates a sample vector that's biased towards the
    // preferred alignment direction (importance sampling).
    vec2 Xi = hammersley(i, SAMPLE_COUNT);
    vec3 H = importanceSampleGGX(Xi, TBN, roughness);
    vec3 L = normalize(2.0 * dot(V, H) * H - V);

    float NdotL = max(L.z, 0.0);
    float NdotH = max(H.z, 0.0);
    float VdotH = max(dot(V, H), 0.0);

    if(NdotL > 0.0)
    {
      float G = geometrySmith_IBL(NdotL, NdotV, roughness);
      float G_Vis = (G * VdotH) / (NdotH * NdotV);
      float Fc = pow(1.0 - VdotH, 5.0);
      A += (1.0 - Fc) * G_Vis;
      B += Fc * G_Vis;
    }
  }
  A /= float(SAMPLE_COUNT);
  B /= float(SAMPLE_COUNT);
  return vec2(A, B);
}

// ----------------------------------------------------------------------------
void main() 
{
  vec2 integratedBRDF = integrateBRDF(inUV.x, inUV.y);
  outFragColor = vec4(integratedBRDF,0.0,1.0);
}