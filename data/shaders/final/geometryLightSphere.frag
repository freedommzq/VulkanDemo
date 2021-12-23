#version 450

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inNormal;

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
	outNormal = vec4(inNormal, 1.0);
	outAlbedo = vec4(0.1, 0.1, 0.1, 1.0);
	outEmissive = vec4(10.0, 0.3, 0.3, 1.0);
}