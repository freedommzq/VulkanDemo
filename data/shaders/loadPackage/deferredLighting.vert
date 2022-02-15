#version 450

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec3 outViewRay;

layout (binding = 0) uniform Camera {
	mat4 view;
	mat4 viewInv;
	mat4 projection;
	mat4 projInv;
	vec3 position;
} uCamera;

void main() 
{
	outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

	vec3 ndc = vec3(outUV * 2.0f - 1.0f, 0.0f);

	vec4 viewRay = uCamera.projInv * vec4(ndc, 1.0);
	viewRay.xyz /= viewRay.w;
	viewRay.xyz /= viewRay.z;
	outViewRay = viewRay.xyz;

	gl_Position = vec4(ndc, 1.0f);
}