#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV0;

layout (set = 0, binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 model;
	mat4 view;
	vec3 camPos;
} ubo;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() 
{
	outWorldPos = vec3(ubo.model * vec4(inPos, 1.0));
	outNormal = mat3(ubo.model) * inNormal;
	outUV0 = inUV0;
	gl_Position =  ubo.projection * ubo.view * vec4(outWorldPos, 1.0);
}
