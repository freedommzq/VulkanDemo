#version 450

layout (location = 0) in vec4 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec3 inTangent;

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec3 outNormal;
layout (location = 3) out vec3 outTangent;

layout (binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 model;
	mat4 view;
	vec4 instancePos[3];
} ubo;


void main() 
{
	vec4 tmpPos = inPos + ubo.instancePos[gl_InstanceIndex];
	mat4 MV = ubo.view * ubo.model;

	gl_Position = ubo.projection * MV * tmpPos;
	
	outWorldPos = vec3(MV * tmpPos);
	outUV = inUV;

	mat3 mNormal = transpose(inverse(mat3(MV)));
	outNormal = mNormal * normalize(inNormal);	
	outTangent = mNormal * normalize(inTangent);
}
