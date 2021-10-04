#version 450

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

layout (binding = 4) uniform UBO 
{
	int ssrBlurSize;
	int ssaoBlurSize;
	float blendFactor;
	int useSsao;
} ubo;

layout (binding = 1) uniform sampler2D samplerDirectColor;
layout (binding = 2) uniform sampler2D samplerReflectColor;
layout (binding = 3) uniform sampler2D samplerSsao;

vec3 blurReflect(){
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
	return reflectColor;
}

float blurAO(){
	vec2 uvPerTexel = 1.0 / textureSize(samplerSsao, 0);
	float ao = 0.0;
	int count = 0;
	for(int i = -ubo.ssaoBlurSize; i <= ubo.ssaoBlurSize; ++i){
		for(int j = -ubo.ssaoBlurSize; j <= ubo.ssaoBlurSize; ++j){
			ao += texture(samplerSsao, inUV + vec2(i, j) * uvPerTexel).x;
			++count;
		}
	}
	ao /= count;
	return ao;
}

void main(){
	vec3 directColor = texture(samplerDirectColor, inUV).xyz;
	vec3 reflectColor = blurReflect();
	float ao = ubo.useSsao == 1 ? blurAO() : 1.0;

	//outFragcolor = vec4((ubo.blendFactor * reflectColor + directColor), 1.0);
	outFragcolor = vec4(ao * (directColor + ubo.blendFactor * reflectColor), 1.0);
	//outFragcolor = vec4(mix(reflectColor, directColor, ubo.blendFactor), 1.0);
	//outFragcolor = vec4(vec3(texture(samplerSsao, inUV).x), 1.0);
}