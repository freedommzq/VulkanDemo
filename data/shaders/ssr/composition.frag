#version 450

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

layout (binding = 4) uniform UBO 
{
	int size;
	float roughness;
} ubo;

layout (binding = 1) uniform sampler2D samplerDirectColor;
layout (binding = 2) uniform sampler2D samplerReflectColor;

void main(){
	vec2 screenSize = textureSize(samplerReflectColor, 0);

	vec2 fragCoord = inUV * screenSize;
	vec3 reflectColor = vec3(0.0);
	int count = 0;
	for(int i = -ubo.size; i <= ubo.size; ++i){
		for(int j = -ubo.size; j <= ubo.size; ++j){
			reflectColor += texture(samplerReflectColor, (fragCoord + vec2(i, j)) / screenSize).rgb;
			++count;
		}
	}
	reflectColor /= count;	

	vec3 directColor = texture(samplerDirectColor, inUV).rgb;

	outFragcolor = vec4(mix(reflectColor, directColor, ubo.roughness), 1.0);
	//outFragcolor = vec4(reflectColor, 1.0);
	//outFragcolor = reflectUV;
}