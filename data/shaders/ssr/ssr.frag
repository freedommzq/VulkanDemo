#version 450

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;


layout (binding = 3) uniform UBO 
{
	mat4 projection;

	// SSR settings
	float maxDistance;
	float resolution;
	float thickness;
} ubo;

layout (binding = 1) uniform sampler2D samplerPosition;
layout (binding = 2) uniform sampler2D samplerNormal;

void main() 
{
	vec2 screenSize = textureSize(samplerPosition, 0);

	// View space
	vec3 fragPos = texture(samplerPosition, inUV).rgb;
	vec3 normal = texture(samplerNormal, inUV).rgb;

	vec3 viewRay = normalize(fragPos);
	vec3 reflectRayUnit = normalize(reflect(viewRay, normal));

	vec4 startPos = vec4(fragPos, 1.0);
	vec4 endPos = vec4(fragPos + ubo.maxDistance * reflectRayUnit, 1.0);

	vec4 startFrag = ubo.projection * startPos;
	startFrag.xyz /= startFrag.w;
	startFrag.xy = startFrag.xy * 0.5 + 0.5;
	startFrag.xy *= screenSize;

	vec4 endFrag = ubo.projection * endPos;
	endFrag.xyz /= endFrag.w;
	endFrag.xy = endFrag.xy * 0.5 + 0.5;
	endFrag.xy *= screenSize;

	float deltaX = endFrag.x - startFrag.x;
	float deltaY = endFrag.y - startFrag.y;
	float useX = abs(deltaX) > abs(deltaY) ? 1.0 : 0.0;
	float delta = mix(abs(deltaY), abs(deltaX), useX) * clamp(ubo.resolution, 0.0, 1.0);
	vec2 increment = vec2(deltaX, deltaY) / max(delta, 0.001);

	vec2 marchFrag = startFrag.xy;
	vec4 uv = vec4(0.0, 0.0, 0.0, 1.0);
	float hit = 0.0;

	for(int i = 0; i < int(delta); ++i){
		marchFrag += increment;
		uv.xy = marchFrag / screenSize;
		if(uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
			break;

		vec4 marchPos = vec4(texture(samplerPosition, uv.xy).rgb, 1.0);

		float percentage = mix((marchFrag.y - startFrag.y) / deltaY, (marchFrag.x - startFrag.x) / deltaX, useX);
		percentage = clamp(percentage, 0.0, 1.0);
		float marchDepthViewSpace = (startPos.z * endPos.z) / mix(endPos.z, startPos.z, percentage);

		float depthDelta = marchDepthViewSpace - marchPos.z;
		if(depthDelta < 0){
			hit = depthDelta > -ubo.thickness ? 1.0 : 0.0;
			break;
		}
	}

	float visibility = 
		hit *
		(1 - max(dot(-viewRay, reflectRayUnit), 0));

	uv.w = visibility;

	outFragcolor = uv;
}