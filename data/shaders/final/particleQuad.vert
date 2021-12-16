#version 450

// Per-vertex attribute
layout (location = 0) in vec2 inUV;
// Per-instance attribute
layout (location = 1) in vec3 inPos;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec4 outColor;

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

layout (constant_id = 0) const float SIMU_DELTA_TIME = 0.1f;

void main()
{
    float offset = inPos.z;

    vec4 v_previous = vec4(inPos, 1.0);
    v_previous.z = fract((ubo.time - cell.startTime) * ubo.inversePeriod - offset);
    
    vec4 v_current =  v_previous;
    v_current.z += (SIMU_DELTA_TIME * ubo.inversePeriod);
    
    vec4 v1 = cell.modelView * v_current;
	vec4 v2 = cell.modelView * v_previous;

    vec3 dv = v2.xyz - v1.xyz;
    
    vec2 dv_normalized = normalize(dv.xy);
    dv.xy += dv_normalized * ubo.particleSize;
    vec2 dp = vec2( -dv_normalized.y, dv_normalized.x ) * ubo.particleSize;
    
    float area = length(dv.xy);
    outColor = ubo.particleColor;
    outColor.a = 0.05 + ubo.particleSize / area;

	outUV = inUV;

    v1.xyz += dv * outUV.y;
    v1.xy += dp * outUV.x;
    
    gl_Position = ubo.projection * v1;
	//gl_ClipVertex = v1;
}
