#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inViewRay;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec3 outViewRay;

void main() 
{
	outUV = inUV;
	outViewRay = inViewRay;

	gl_Position = vec4(inPos, 1.0f);
}
