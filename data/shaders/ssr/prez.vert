#version 450

layout (location = 0) in vec4 inPos;

layout (binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 model;
	mat4 view;
	vec4 instancePos[3];
} ubo;

void main(){
	vec4 tmpPos = inPos + ubo.instancePos[gl_InstanceIndex];

	gl_Position = ubo.projection * ubo.view * ubo.model * tmpPos;
}