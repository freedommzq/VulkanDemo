#version 450

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

layout (binding = 1) uniform sampler2D samplerDirectLight;
layout (binding = 2) uniform sampler2D samplerReflectUV;

void main(){
	vec3 directColor = texture(samplerDirectLight, inUV).rgb;
	vec4 reflectUV = texture(samplerReflectUV, inUV);

	vec3 reflectColor = texture(samplerDirectLight, reflectUV.xy).rgb;
	reflectColor = mix(vec3(0.0), reflectColor, reflectUV.w);

	//outFragcolor = vec4(directColor + reflectColor, 1.0);
	outFragcolor = vec4(reflectColor, 1.0);
}