#version 450

// Per-Vertex
layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inUV0;
layout (location = 3) in vec3 inUV1;
layout (location = 4) in vec3 inUV2;
layout (location = 5) in vec3 inUV3;
// Per-Instance
layout (location = 6) in vec3 inInstancePos;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outUV0;
layout (location = 2) out vec2 outUV1;
layout (location = 3) out vec2 outUV2;
layout (location = 4) out vec2 outUV3;
layout (location = 5) flat out uint outTexIndex0;
layout (location = 6) flat out uint outTexIndex1;
layout (location = 7) flat out uint outTexIndex2;
layout (location = 8) flat out uint outTexIndex3;

layout (set = 0, binding = 0) uniform Camera {
	mat4 view;
	mat4 viewInv;
	mat4 projection;
	mat4 projInv;
	vec3 position;
} uCamera;

layout(push_constant) uniform Model {
	mat4 model;
} uModel;

void main() 
{
	outUV0 = inUV0.xy;
	outUV1 = inUV1.xy;
	outUV2 = inUV2.xy;
	outUV3 = inUV3.xy;
	outTexIndex0 = uint(inUV0.z);
	outTexIndex1 = uint(inUV1.z);
	outTexIndex2 = uint(inUV2.z);
	outTexIndex3 = uint(inUV3.z);

	vec3 worldPos = (uModel.model * vec4(inPos, 1.0)).xyz + inInstancePos;
	gl_Position = uCamera.projection * uCamera.view * vec4(worldPos, 1.0);

	outNormal = mat3(inverse(transpose(uModel.model))) * inNormal;
}
