#version 450

layout (binding = 1) uniform sampler2D samplerColor;
layout (binding = 2) uniform sampler2D samplerNormalMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec3 inWorldPos;
layout (location = 4) in vec3 inTangent;

layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec4 outAlbedo;
layout (location = 3) out vec4 outEmissive;

#define NEAR 0.1
#define FAR 64.0

float linearDepth(float depth) {
	float z = depth * 2.0f - 1.0f; 
	return (2.0f * NEAR * FAR) / (FAR + NEAR - z * (FAR - NEAR));	
}

void main() 
{
	outPosition = vec4(inWorldPos, linearDepth(gl_FragCoord.z));

	// Calculate normal in tangent space
	vec3 N = normalize(inNormal);
	vec3 T = normalize(inTangent);
	vec3 B = cross(N, T);
	mat3 TBN = mat3(T, B, N);
	vec3 tnorm = TBN * normalize(texture(samplerNormalMap, inUV).xyz * 2.0 - vec3(1.0));
	//outNormal = vec4(tnorm, 1.0);
	outNormal = vec4(N, 1.0);

	outAlbedo = texture(samplerColor, inUV);
	outEmissive = vec4(vec3(0.0), 1.0);
}