#version 450

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

layout (binding = 4) uniform UBO {
	int blurSize;
} ubo;

layout (binding = 1) uniform sampler2D samplerSsao;

void main(){
	vec2 uvPerTexel = 1.0 / textureSize(samplerSsao, 0);
	float ao = 0.0;
	int count = 0;
	for(int i = -ubo.blurSize; i <= ubo.blurSize; ++i){
		for(int j = -ubo.blurSize; j <= ubo.blurSize; ++j){
			ao += texture(samplerSsao, inUV + vec2(i, j) * uvPerTexel).x;
			++count;
		}
	}
	ao /= count;

	outFragcolor = vec4(vec3(ao), 1.0);
}