#version 450

layout (constant_id = 0) const uint TEXTURE_COUNT = 50;

layout (set = 1, binding = 0) uniform sampler2D textures[TEXTURE_COUNT];

layout (location = 0) in vec2 inUV0;
layout (location = 1) in vec2 inUV1;
layout (location = 2) in vec2 inUV2;
layout (location = 3) in vec2 inUV3;
layout (location = 4) flat in uint inTexIndex0;
layout (location = 5) flat in uint inTexIndex1;
layout (location = 6) flat in uint inTexIndex2;
layout (location = 7) flat in uint inTexIndex3;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	vec4 color = texture(textures[inTexIndex0], inUV0);
	color *= texture(textures[inTexIndex1], inUV1);
	//color *= texture(textures[inTexIndex2], inUV2);
	//color *= texture(textures[inTexIndex3], inUV3);

	outFragColor = color;
}