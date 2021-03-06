#version 450

const mat3 weight = mat3(
	1.0, 2.0, 1.0,
	2.0, 4.0, 2.0,
	1.0, 2.0, 1.0
);

layout (constant_id = 0) const uint CHANNEL_COUNT = 4;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

layout (binding = 5) uniform UBO {
	int blurSize;
} ubo;

layout (binding = 1) uniform sampler2D samplerRaw;

void main(){
	vec4 mask;
	switch(CHANNEL_COUNT){
		case 1: mask = vec4(1.0, 0.0, 0.0, 0.0); break;
		case 2: mask = vec4(1.0, 1.0, 0.0, 0.0); break;
		case 3: mask = vec4(1.0, 1.0, 1.0, 0.0); break;
		case 4: mask = vec4(1.0); break;
		default: mask = vec4(0.0);
	}

	vec2 uvPerTexel = 1.0 / textureSize(samplerRaw, 0);

	/*
	vec4 result = vec4(0.0);
	for(int i = -ubo.blurSize; i <= ubo.blurSize; ++i){
		for(int j = -ubo.blurSize; j <= ubo.blurSize; ++j){
			result += texture(samplerRaw, inUV + (vec2(i, j) + vec2(0.5)) * uvPerTexel);
		}
	}
	result /= ((2 * ubo.blurSize + 1) * (2 * ubo.blurSize + 1));
	*/

	vec4 result = vec4(0.0);
	for(int i = 0; i < 3; ++i){
		for(int j = 0; j < 3; ++j){
			result += texture(samplerRaw, inUV + (vec2(i - 1, j - 1) + vec2(0.5)) * uvPerTexel) * weight[i][j];
		}
	}
	result /= 16.0;

	result *= mask;
	result += (vec4(1.0) - mask);

	outFragcolor = result;
}