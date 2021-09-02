#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inColor;

layout (set = 0, binding = 0) uniform UBO {
	mat4 projectionMatrix;
	mat4 viewMatrix;
} ubo;

layout(push_constant) uniform PushConsts {
	mat4 model;
	int texID;
} primitive;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec3 outColor;

out gl_PerVertex {
    vec4 gl_Position;   
};

void main() {
	outUV = inUV;
	outColor = inColor;
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * primitive.model * vec4(inPos.xyz, 1.0);
}
