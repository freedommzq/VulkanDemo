#version 450

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

layout (binding = 4) uniform UBO {
	int ssrBlurSize;
	float blendFactor;
} ubo;

layout (binding = 1) uniform sampler2D samplerDirectColor;
layout (binding = 2) uniform sampler2D samplerReflectColor;

void main(){
	vec3 directColor = texture(samplerDirectColor, inUV).xyz;

	vec2 uvPerTexel = 1.0 / textureSize(samplerReflectColor, 0);
	vec3 reflectColor = vec3(0.0);
	int count = 0;
	for(int i = -ubo.ssrBlurSize; i <= ubo.ssrBlurSize; ++i){
		for(int j = -ubo.ssrBlurSize; j <= ubo.ssrBlurSize; ++j){
			reflectColor += texture(samplerReflectColor, inUV + vec2(i, j) * uvPerTexel).xyz;
			++count;
		}
	}
	reflectColor /= count;

	outFragcolor = vec4(directColor + ubo.blendFactor * reflectColor, 1.0);
	//outFragcolor = vec4(directColor, 1.0);
	//outFragcolor = vec4(mix(reflectColor, directColor, ubo.blendFactor), 1.0);
}