#version 450

// Per-vertex attribute
layout (location = 0) in vec2 inUV;
// Per-instance attribute
layout (location = 1) in vec3 inPos;

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform UBO
{
	mat4 projection;

	vec4 particleColor;
	float particleSize;
	float inversePeriod;
		
	float time;
} ubo;

layout (push_constant) uniform Cell {
	mat4 modelView;
	float startTime;
} cell;

void main()
{
    float offset = inPos.z;

    vec4 v_current = vec4(inPos, 1.0);
    v_current.z = fract((ubo.time - cell.startTime) * ubo.inversePeriod - offset);
    
    gl_Position = ubo.projection * cell.modelView * v_current;

    float pointSize = abs(1280.0 * ubo.particleSize / gl_Position.w);

    //gl_PointSize = max(ceil(pointSize),2);
    gl_PointSize = ceil(pointSize);
    
	outColor = ubo.particleColor;
    outColor.a = 0.05 + (pointSize * pointSize)/(gl_PointSize * gl_PointSize);
}
