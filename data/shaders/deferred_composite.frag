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

layout (binding = 0) uniform CameraUbo
{
  mat4 viewMatrix;
  mat4 viewMatrixInverse;
  mat4 projectionMatrix;
  vec4 observerPosition;
} camera;

layout (std430,binding = 1) readonly buffer Lights
{
	LightPoint lights[];
};

layout(input_attachment_index = 2, binding = 2) uniform subpassInputMS inPosition;
layout(input_attachment_index = 3, binding = 3) uniform subpassInputMS inNormal;
layout(input_attachment_index = 4, binding = 4) uniform subpassInputMS inFragColor;


layout (location = 0) out vec4 outColor;

void main() 
{
  vec3 worldPosition = subpassLoad(inPosition,gl_SampleID).xyz;
  vec3 worldNormal   = subpassLoad(inNormal,gl_SampleID).xyz;
  worldNormal        = normalize(worldNormal * 2.0 - vec3(1.0));
  vec4 color         = subpassLoad(inFragColor,gl_SampleID);

  vec3 viewPosition = camera.viewMatrixInverse[3].xyz / camera.viewMatrixInverse[3].w;
  vec3 viewDir      = normalize(viewPosition - worldPosition);

  vec4 finalColor = vec4(0.0,0.0,0.0,1.0);
  for(uint i=0; i<NUM_LIGHTS; i++)
  {
    vec3 lightDir       = lights[i].position.xyz - worldPosition;
    float lightDistance = length(lightDir);
    lightDir            = normalize(lightDir);
    float attenuation   = 1.0f / ( lights[i].attenuation.x + lightDistance*lights[i].attenuation.y + lightDistance*lightDistance*lights[i].attenuation.z  );

    vec3 diffuse        = max(dot(worldNormal, lightDir), 0.0) * color.xyz * lights[i].color.xyz * attenuation;

    vec3 reflectDir     = reflect(-lightDir, worldNormal); 
    vec3 specular       = pow(max(dot(viewDir, reflectDir), 0.0), 128.0) * color.a * lights[i].color.xyz * attenuation;

    finalColor.xyz      += diffuse + specular;
  }
  // Reinhard tone mapping
  finalColor.xyz = finalColor.xyz / (finalColor.xyz + vec3(1.0));
  // gamma correction
  outColor.xyz = pow(finalColor.xyz, vec3(1.0/2.2));
  outColor.a = finalColor.a;
}