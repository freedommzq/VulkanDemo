#version 450

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

layout (binding = 5) uniform UBO {
	float blendFactor;
} ubo;

layout (binding = 1) uniform sampler2D samplerDirectColor;
layout (binding = 2) uniform sampler2D samplerReflectColor;

void main(){
	vec3 directColor = texture(samplerDirectColor, inUV).xyz;
	vec3 reflectColor = texture(samplerReflectColor, inUV).xyz;

	vec3 hdrColor = directColor + ubo.blendFactor * reflectColor;

	outFragcolor = vec4(hdrColor, 1.0);
}