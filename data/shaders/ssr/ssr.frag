#version 450

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec3 inViewRay;

layout (location = 0) out vec4 outFragcolor;


layout (binding = 4) uniform UBO 
{
	mat4 projection;

	// SSR settings
	float maxDistance;
	float resolution;
	float thickness;
} ubo;

layout (binding = 1) uniform sampler2D samplerNormal;
layout (binding = 2) uniform sampler2D samplerDepth;
layout (binding = 3) uniform sampler2D samplerDirectColor;

void main() 
{
	vec2 screenSize = textureSize(samplerNormal, 0);

	// Reflect color
	vec3 reflectColor = vec3(0.0);

	// View space
	vec4 N = texture(samplerNormal, inUV);
	vec3 normal = N.xyz;
	float depth = texture(samplerDepth, inUV).x;

	if(N.w != 1.0){
		outFragcolor = vec4(reflectColor, 1.0);
		return;
	}	

	float linearDepth = -ubo.projection[3][2] / (depth + ubo.projection[2][2]);
	vec3 fragPos = inViewRay * linearDepth;

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
	vec2 uv = vec2(0.0);
	float hit = 0.0;

	/*
	int level = 0;
	while(level > -1){
		vec2 marchFragTemp = marchFrag + increment * pow(2, level);
		vec2 uvTemp = marchFragTemp / screenSize;
		float depth = textureLod(samplerDepth, uvTemp, level).r;
		float linearDepth = -ubo.projection[3][2] / (depth + ubo.projection[2][2]);

		float percentage = mix((marchFragTemp.y - startFrag.y) / deltaY, (marchFragTemp.x - startFrag.x) / deltaX, useX);
		percentage = clamp(percentage, 0.0, 1.0);
		float marchDepthViewSpace = (startPos.z * endPos.z) / mix(endPos.z, startPos.z, percentage);

		float depthDelta = marchDepthViewSpace - linearDepth;
		if(depthDelta < 0){
			if(level == 0){
				marchFrag = marchFragTemp;
				uv = uvTemp;
				hit = depthDelta > -ubo.thickness ? 1.0 : 0.0;
				//hit = 1;
			}
			--level;
		}
		else{
			marchFrag = marchFragTemp;
			uv = uvTemp;
			if(uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
				break;
			++level;
		}
	}
	*/

	for(int i = 0; i < int(delta); ++i){
		marchFrag += increment;
		uv = marchFrag / screenSize;
		if(uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
			break;

		float depth = texture(samplerDepth, uv).r;
		float linearDepth = -ubo.projection[3][2] / (depth + ubo.projection[2][2]);

		float percentage = mix((marchFrag.y - startFrag.y) / deltaY, (marchFrag.x - startFrag.x) / deltaX, useX);
		percentage = clamp(percentage, 0.0, 1.0);
		float marchDepthViewSpace = (startPos.z * endPos.z) / mix(endPos.z, startPos.z, percentage);

		float depthDelta = marchDepthViewSpace - linearDepth;
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
	//outFragcolor = vec4(vec3(-linearDepth), 1.0);
}