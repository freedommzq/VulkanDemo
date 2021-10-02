#version 450

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;


layout (binding = 4) uniform UBO 
{
	mat4 projection;
	mat4 view;
	vec4 viewPos;

	// SSR settings
	float maxDistance;
	float resolution;
	float thickness;
} ubo;

layout (binding = 1) uniform sampler2D samplerPosition;
layout (binding = 2) uniform sampler2D samplerNormal;
layout (binding = 3) uniform sampler2D samplerDirectColor;

void main() 
{
	vec2 screenSize = textureSize(samplerNormal, 0);

	// Reflect color
	vec3 reflectColor = vec3(0.0);

	// World space
	vec4 P = texture(samplerPosition, inUV);
	if(P.w != 1.0){
		outFragcolor = vec4(reflectColor, 1.0);
		return;
	}

	// Transform to view space
	vec3 fragPos = vec3(ubo.view * P);
	mat3 mNormal = transpose(inverse(mat3(ubo.view)));
	vec3 normal = mNormal * texture(samplerNormal, inUV).xyz;

	vec3 viewRay = normalize(fragPos);
	vec3 reflectRayUnit = reflect(viewRay, normal);

	vec4 startPos = vec4(fragPos, 1.0);
	vec4 endPos = vec4(fragPos + ubo.maxDistance * reflectRayUnit, 1.0);

	vec2 startFrag = inUV * screenSize;

	vec4 endFrag = ubo.projection * endPos;
	endFrag.xyz /= endFrag.w;
	endFrag.xy = endFrag.xy * 0.5 + 0.5;
	endFrag.xy *= screenSize;

	float deltaX = endFrag.x - startFrag.x;
	float deltaY = endFrag.y - startFrag.y;
	float useX = abs(deltaX) > abs(deltaY) ? 1.0 : 0.0;
	float delta = mix(abs(deltaY), abs(deltaX), useX) * clamp(ubo.resolution, 0.0, 1.0);
	vec2 increment = vec2(deltaX, deltaY) / max(delta, 0.001);

	vec2 marchFrag = startFrag;
	vec2 uv = vec2(0.0);
	float hit = 0.0;

	for(int i = 0; i < int(delta); ++i){
		marchFrag += increment;
		uv = marchFrag / screenSize;
		if(uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
			break;

		float percentage = mix((marchFrag.y - startFrag.y) / deltaY, (marchFrag.x - startFrag.x) / deltaX, useX);
		percentage = clamp(percentage, 0.0, 1.0);
		float marchDepthViewSpace = (startPos.z * endPos.z) / mix(endPos.z, startPos.z, percentage);

		vec3 marchPos = vec3(ubo.view * texture(samplerPosition, uv));

		float depthDelta = marchDepthViewSpace - marchPos.z;
		if(depthDelta < 0){
			hit = depthDelta > -ubo.thickness ? 1.0 : 0.0;
			break;
		}
	}

	float visibility = 
		hit *
		(1 - max(dot(-viewRay, reflectRayUnit), 0));

	reflectColor = texture(samplerDirectColor, uv).rgb * visibility;
	outFragcolor = vec4(reflectColor, 1.0);
}