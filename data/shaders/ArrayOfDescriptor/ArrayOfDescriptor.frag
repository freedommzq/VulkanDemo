#version 450

layout (set = 0, binding = 1) uniform sampler2D textures[2];

layout (location = 0) in vec2 inUV;
layout (location = 1) flat in uint inTexID;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	//vec4 color = outFragColor = vec4(inUV, 0.0, 1.0);
	vec4 color = texture(textures[inTexID], inUV);
	outFragColor = color;
}