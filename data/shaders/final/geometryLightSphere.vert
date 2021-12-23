#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec3 outNormal;

layout (binding = 0) uniform PerScene 
{
	mat4 view;
	mat4 projection;
	vec4 instancePos[3];
} perScene;

layout (push_constant) uniform PerModel {
	mat4 model;
} perModel;

void main() 
{
	outWorldPos = vec3(perModel.model * vec4(inPos, 1.0));
	outNormal = mat3(perModel.model) * inNormal;
	gl_Position =  perScene.projection * perScene.view * vec4(outWorldPos, 1.0);
}