#version 450

// per vertex
layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;

// per instance
layout (location = 2) in vec3 inModelPos;
layout (location = 3) in uint inTexID;

layout (set = 0, binding = 0) uniform UBO {
	mat4 projectionMatrix;
	mat4 viewMatrix;
} ubo;

layout (location = 0) out vec2 outUV;
layout (location = 1) flat out uint outTexID;

out gl_PerVertex {
    vec4 gl_Position;   
};

void main() {
	outUV = inUV;
	outTexID = inTexID;
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * vec4(inPos.xyz + inModelPos, 1.0);
}
