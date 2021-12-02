// viewRay -> view ray
// ray     -> reflect ray

// WS -> World Space
// VS -> View Space
// SS -> Screen Space

#version 450
#extension GL_EXT_samplerless_texture_functions : enable

#define DEPTH_HIERARCHY

layout (location = 0) in vec2 uv;
layout (location = 1) in vec3 viewRayUnitZ;

layout (location = 0) out vec4 outFragcolor;


layout (binding = 4) uniform UBO 
{
	mat4 projection;
	mat4 projInv;

	// SSR settings
	float maxDistance;
	float resolution;
	float thickness;

	int mostDetailedMip;
	int maxTraversalStep;
} ubo;

layout (binding = 1) uniform sampler2D samplerNormal;
layout (binding = 2) uniform sampler2D samplerDepth;
layout (binding = 3) uniform sampler2D samplerDirectColor;

vec3 projectPosition(vec3 pos){
	vec4 projected = ubo.projection * vec4(pos, 1.0f);
	projected.xyz /= projected.w;
	projected.xy = projected.xy * 0.5 + 0.5;
	return projected.xyz;
}

vec3 projectDirection(vec3 origin, vec3 direction, vec3 originSS){
	vec3 end = projectPosition(origin + direction);
	return end - originSS;
}

vec3 unprojectPositionToViewSpace(vec3 coord) {
    coord.xy = 2 * coord.xy - 1;
    vec4 projected = ubo.projInv * vec4(coord, 1.0);
    projected.xyz /= projected.w;
    return projected.xyz;
}

#ifndef DEPTH_HIERARCHY

void main() 
{
	vec2 screenSize = textureSize(samplerNormal, 0);

	// Reflect radiance
	vec3 reflectRadiance = vec3(0.0);

	// View space
	vec4 N = texture(samplerNormal, uv);
	vec3 normal = normalize(N.xyz);
	float depth = texture(samplerDepth, uv).x;

	if(N.w != 1.0){
		outFragcolor = vec4(reflectRadiance, 1.0);
		return;
	}	

	float linearDepth = -ubo.projection[3][2] / (depth + ubo.projection[2][2]);
	vec3 posVS = viewRayUnitZ * linearDepth;

	vec3 viewRay = normalize(posVS);
	vec3 rayDir = reflect(viewRay, normal);

	vec3 startPos = posVS;
	vec3 start = projectPosition(startPos);
	vec2 startFrag = start.xy * screenSize;

	vec3 endPos = posVS + ubo.maxDistance * rayDir;
	vec3 end = projectPosition(endPos);
	vec2 endFrag = end.xy * screenSize;

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
		(1 - max(dot(-viewRay, rayDir), 0));

	reflectRadiance = texture(samplerDirectColor, uv).rgb * visibility;

	outFragcolor = vec4(reflectRadiance, 1.0);
}

#else

#define FFX_SSSR_FLOAT_MAX 3.402823466e+38

void initAdvanceRay(vec3 originSS, vec3 rayDirSS, vec3 rayDirInvSS, vec2 curMipResolution, vec2 curMipResolutionInv,
	vec2 floorOffset, vec2 uvOffset, out vec3 curPos, out float curT){

    vec2 curMipPos = curMipResolution * originSS.xy;

    // Intersect ray with the half box that is pointing away from the ray origin.
    vec2 xy_plane = floor(curMipPos) + floorOffset;
    xy_plane = xy_plane * curMipResolutionInv + uvOffset;

    // o + d * t = p' => t = (p' - o) / d
    vec2 t = (xy_plane - originSS.xy) * rayDirInvSS.xy;
    curT = min(t.x, t.y);
    curPos = originSS + curT * rayDirSS;
}

bool advanceRay(vec3 originSS, vec3 rayDirSS, vec3 rayDirInvSS, vec2 curMipPos, vec2 curMipResolutionInv,
	vec2 floorOffset, vec2 uvOffset, float curSurfaceZ, inout vec3 curPos, inout float curT){
	
    // Create boundary planes
    vec2 xy_plane = floor(curMipPos) + floorOffset;
    xy_plane = xy_plane * curMipResolutionInv + uvOffset;
    vec3 boundary_planes = vec3(xy_plane, curSurfaceZ);

    // Intersect ray with the half box that is pointing away from the ray origin.
    // o + d * t = p' => t = (p' - o) / d
    vec3 t = (boundary_planes - originSS) * rayDirInvSS;

    // Prevent using z plane when shooting out of the depth buffer.
    t.z = rayDirSS.z > 0 ? t.z : FFX_SSSR_FLOAT_MAX;

    // Choose nearest intersection with a boundary.
    float t_min = min(min(t.x, t.y), t.z);

    // Smaller z means closer to the camera.
    bool above_surface = curPos.z < curSurfaceZ;

    // Decide whether we are able to advance the ray until we hit the xy boundaries or if we had to clamp it at the surface.
    bool skipped_tile = t_min != t.z && above_surface;

    // Make sure to only advance the ray if we're still above the surface.
    curT = above_surface ? t_min : curT;

    // Advance ray
    curPos = originSS + curT * rayDirSS;

    return skipped_tile;
}

vec3 rayMarch(vec3 originSS, vec3 rayDirSS, vec2 screenSize, out bool validHit){
	const vec3 rayDirInvSS = vec3(
		rayDirSS.x != 0.0 ? 1.0 / rayDirSS.x : FFX_SSSR_FLOAT_MAX,
		rayDirSS.y != 0.0 ? 1.0 / rayDirSS.y : FFX_SSSR_FLOAT_MAX,
		rayDirSS.z != 0.0 ? 1.0 / rayDirSS.z : FFX_SSSR_FLOAT_MAX
	);

	int curMip = ubo.mostDetailedMip;

	vec2 curMipResolution = screenSize * pow(0.5, curMip);
	vec2 curMipResolutionInv = vec2(1.0 / curMipResolution.x, 1.0 / curMipResolution.y);

	// Offset to the bounding boxes uv space to intersect the ray with the center of the next pixel.
    // This means we ever so slightly over shoot into the next region. 
    vec2 uvOffset = 0.005 * exp2(ubo.mostDetailedMip) / screenSize;
    uvOffset = vec2(
		rayDirSS.x < 0 ? -uvOffset.x : uvOffset.x,
		rayDirSS.y < 0 ? -uvOffset.y : uvOffset.y
	);

    // Offset applied depending on current mip resolution to move the boundary to the left/right upper/lower border depending on ray direction.
    vec2 floorOffset = vec2(
		rayDirSS.x < 0 ? 0 : 1,
		rayDirSS.y < 0 ? 0 : 1
	);

	// Initially advance ray to avoid immediate self intersections.
	float curT;
	vec3 curPos; // ray end position in Screen Space
	initAdvanceRay(originSS, rayDirSS, rayDirInvSS, curMipResolution, curMipResolutionInv, floorOffset, uvOffset, curPos, curT);

	int i = 0;
	while(i < ubo.maxTraversalStep && curMip >= ubo.mostDetailedMip){
		vec2 curMipPos = curMipResolution * curPos.xy;
		float curSurfaceZ = texelFetch(samplerDepth, ivec2(curMipPos), curMip).x;
		bool success = advanceRay(originSS, rayDirSS, rayDirInvSS, curMipPos, curMipResolutionInv, floorOffset, uvOffset, curSurfaceZ, curPos, curT);
		curMip += success ? 1 : -1;
		curMipResolution *= success ? 0.5f : 2.0f;
		curMipResolutionInv *= success ? 2.0f : 0.5f;
		++i;
	}

	validHit = (i <= ubo.maxTraversalStep);

	return curPos;
}

float validateHit(vec3 hit, vec2 uv, vec3 rayDirView, vec2 screenSize){
    // Reject hits outside the view frustum
    if (hit.x < 0.0 || hit.x > 1.0 || hit.y < 0.0 || hit.y > 1.0) {
        return 0;
    }

    // Reject the hit if we didnt advance the ray significantly to avoid immediate self reflection
    vec2 manhattanDist = abs(hit.xy - uv);
    if(manhattanDist.x < (2 / screenSize.x) && manhattanDist.y < (2 / screenSize.y)) {
        return 0;
    }

    // Don't lookup radiance from the background.
    ivec2 texCoords = ivec2(screenSize * hit.xy);
    float z = texelFetch(samplerDepth, texCoords / 2, 1).x;
    if (z == 1.0) {
        return 0;
    }

    // We check if we hit the surface from the back, these should be rejected.
    vec3 hitNormal = texture(samplerNormal, hit.xy).xyz;
    if (dot(hitNormal, rayDirView) > 0) {
        return 0;
    }

    vec3 hitSurfaceVS = unprojectPositionToViewSpace(vec3(hit.xy, z));
    vec3 hitVS = unprojectPositionToViewSpace(hit);
    float distance = length(hitSurfaceVS - hitVS);

    // Fade out hits near the screen borders
    vec2 fov = 0.05 * vec2(screenSize.y / screenSize.x, 1);
    vec2 border = smoothstep(vec2(0.0), fov, hit.xy) * (1 - smoothstep(vec2(1.0) - fov, vec2(1.0), hit.xy));
    float vignette = border.x * border.y;

    // We accept all hits that are within a reasonable minimum distance below the surface.
    // Add constant in linear space to avoid growing of the reflections toward the reflected objects.
    float confidence = 1 - smoothstep(0, ubo.thickness, distance);
    confidence *= confidence;

    return vignette * confidence;

	return 1;
}

void main() 
{
	// Reflect color
	vec3 reflectRadiance = vec3(0.0);

	// View space
	vec4 N = texture(samplerNormal, uv);
	vec3 normal = normalize(N.xyz);

	if(N.w != 1.0){
		outFragcolor = vec4(reflectRadiance, 1.0);
		return;
	}

	vec2 screenSize = textureSize(samplerNormal, 0);

	vec2 mipResolution = screenSize * pow(0.5, ubo.mostDetailedMip);
	float z = texelFetch(samplerDepth, ivec2(uv * mipResolution), ubo.mostDetailedMip).x;

	float linearDepth = -ubo.projection[3][2] / (z + ubo.projection[2][2]);
	vec3 posVS = viewRayUnitZ * linearDepth;

	vec3 viewRay = posVS;
	vec3 viewRayDir = normalize(viewRay);
	vec3 rayDir = reflect(viewRayDir, normal);

	vec3 originSS = vec3(uv, z);
	vec3 rayDirSS = projectDirection(viewRay, rayDir, originSS);

	bool validHit = false;
	vec3 hit = rayMarch(originSS, rayDirSS, screenSize, validHit);

	float confidence = validHit ? validateHit(hit, uv, rayDir, screenSize) : 0;

	reflectRadiance = texture(samplerDirectColor, hit.xy).rgb * confidence;

	outFragcolor = vec4(reflectRadiance, 1.0);
}
#endif