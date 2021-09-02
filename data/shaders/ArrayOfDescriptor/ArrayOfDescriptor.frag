#version 450

layout (set = 0, binding = 1) uniform sampler2D textures[2];

layout(push_constant) uniform PushConsts {
	mat4 model;
	int texID;
} primitive;

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	//outFragColor = vec4(inUV, 0.0, 1.0);
	//outFragColor = vec4(inColor, 1.0);
	vec4 color = texture(textures[primitive.texID], inUV);
	outFragColor = color;
}