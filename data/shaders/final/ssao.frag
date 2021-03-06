#version 450

layout (binding = 1) uniform sampler2D samplerPositionDepth;
layout (binding = 2) uniform sampler2D samplerNormal;
layout (binding = 3) uniform sampler2D ssaoNoise;

#define SSAO_KERNEL_SIZE 32

layout (binding = 5) uniform UBO 
{
	mat4 projection;
	mat4 view;
	float radius;
	float bias; // remove banding
	vec4 samples[SSAO_KERNEL_SIZE];
} ubo;

layout (location = 0) in vec2 inUV;

layout (location = 0) out float outFragColor;

void main() 
{
	// Get G-Buffer values
	vec4 P = texture(samplerPositionDepth, inUV);
	vec4 N = texture(samplerNormal, inUV);

	if(N.w != 1.0){
		outFragColor = 1.0;
		return;
	}

	vec3 fragPos = vec3(ubo.view * vec4(P.xyz, 1.0));
	mat3 mNormal = transpose(inverse(mat3(ubo.view)));
	vec3 normal = mNormal * N.xyz;

	// Get a random vector using a noise lookup
	ivec2 texDim = textureSize(samplerPositionDepth, 0); 
	ivec2 noiseDim = textureSize(ssaoNoise, 0);
	const vec2 noiseUV = vec2(float(texDim.x)/float(noiseDim.x), float(texDim.y)/(noiseDim.y)) * inUV;  
	vec3 randomVec = texture(ssaoNoise, noiseUV).xyz * 2.0 - 1.0;
	
	// Create TBN matrix
	vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	vec3 bitangent = cross(tangent, normal);
	mat3 TBN = mat3(tangent, bitangent, normal);

	float occlusion = 0.0f;
	for(int i = 0; i < SSAO_KERNEL_SIZE; i++)
	{		
		vec3 samplePos = TBN * ubo.samples[i].xyz; 
		samplePos = fragPos + samplePos * ubo.radius; 
		
		// project
		vec4 offset = vec4(samplePos, 1.0f);
		offset = ubo.projection * offset; 
		offset.xyz /= offset.w; 
		offset.xyz = offset.xyz * 0.5f + 0.5f; 
		
		float sampleDepth = -texture(samplerPositionDepth, offset.xy).w; 

		float rangeCheck = smoothstep(0.0f, 1.0f, ubo.radius / abs(fragPos.z - sampleDepth));
		occlusion += (sampleDepth >= samplePos.z + ubo.bias ? 1.0f : 0.0f) * rangeCheck;           
	}
	occlusion = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
	
	outFragColor = occlusion;
}

