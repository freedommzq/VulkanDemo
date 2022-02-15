#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;

layout (location = 0) out vec3 outPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV0;

layout (set = 0, binding = 0) uniform UniformCamera {
	mat4 view;
	mat4 viewInv;
	mat4 projection;
	mat4 projInv;
	vec3 position;
} uCamera;

layout(push_constant) uniform ModelMaterial {
	mat4 model;

	vec4 baseColorFactor;
	int baseColorTextureSet;
	int normalTextureSet;
	int physicalDescriptorTextureSet;
	float metallicFactor;
	float roughnessFactor;
	float alphaMask;
	float alphaMaskCutoff;
} uModelMaterial;

void main() 
{
	outUV0 = inUV0;

	outPos = vec3(uModelMaterial.model * vec4(inPos, 1.0));
	gl_Position = uCamera.projection * uCamera.view * vec4(outPos, 1.0);

	outNormal = mat3(inverse(transpose(uModelMaterial.model))) * inNormal;
}
