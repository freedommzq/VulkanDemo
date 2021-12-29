#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inUV0;
layout (location = 3) in vec3 inUV1;
layout (location = 4) in vec3 inUV2;
layout (location = 5) in vec3 inUV3;

layout (location = 0) out vec2 outUV0;
layout (location = 1) out vec2 outUV1;
layout (location = 2) out vec2 outUV2;
layout (location = 3) out vec2 outUV3;
layout (location = 4) flat out uint outTexIndex0;
layout (location = 5) flat out uint outTexIndex1;
layout (location = 6) flat out uint outTexIndex2;
layout (location = 7) flat out uint outTexIndex3;

//layout (location = 1) out vec3 outNormal;

layout (set = 0, binding = 0) uniform UBO {
	mat4 model;
	mat4 view;
	mat4 projection;
} ubo;

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

	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPos, 1.0);

	//outNormal = mat3(inverse(transpose(ubo.model))) * inNormal;
}
