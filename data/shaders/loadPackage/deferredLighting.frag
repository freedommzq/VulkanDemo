#version 450

const uint MAX_LIST_LENGTH = 256;
const uint CLUSTER_X = 36;
const uint CLUSTER_Y = 20;
const uint CLUSTER_Z = 64;

const uint WIDTH = 1280;
const uint HEIGHT = 720;
const float NEAR = 0.1;
const float FAR = 1500.0;

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec3 inViewRay;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform UniformCamera {
	mat4 view;
	mat4 viewInv;
	mat4 projection;
	mat4 projInv;
	vec3 position;
} uCamera;

layout (set = 0, binding = 1) uniform sampler2D samplerDepth;
layout (set = 0, binding = 2) uniform sampler2D samplerRT0;
layout (set = 0, binding = 3) uniform sampler2D samplerRT1;
layout (set = 0, binding = 4) uniform sampler2D samplerRT2;

layout (set = 1, binding = 0) uniform usampler3D clusterData; // point/spot light count

struct LightList {
	uint indices[MAX_LIST_LENGTH];
};
layout (set = 1, binding = 1) readonly buffer LightLists {
	LightList lightLists[];
};

struct PointLight {
	vec4 positionRange;
	vec4 colorIntensity;
};
layout (set = 1, binding = 2) readonly buffer PointLights {
	PointLight lights[];
} uPointLights;

struct SpotLight {
	vec4 positionRange;
	vec4 colorIntensity;
	vec4 directionCutoff;
};
layout (set = 1, binding = 3) readonly buffer SpotLights {
	SpotLight lights[];
} uSpotLights;

//-----------------------------------------------------------------------

vec3 decodeNormal(vec2 encN){
	vec2 fenc = encN * 2.0;
	float f = dot(fenc, fenc);
	float g = sqrt(1 - f / 4.0);
	return vec3(fenc * g, 1 - f / 2.0);
}

void decodeRoughnessMetallic(float roughnessMetallic, out float perceptualRoughness, out float metallic){
	perceptualRoughness = floor(roughnessMetallic) / 100.0;
	metallic = fract(roughnessMetallic) * 256.0;
}

/*
vec3 calcLightCity(vec3 fragPos, vec3 n, vec3 v, vec3 reflectDir){
	vec3 lightDirVS = normalize(lightPosVS);

	float distance = length(lightPosVS - posVS);
	float attenuation = clamp(1.0f - distance * distance * (1.0f / (uLight.lightRange * uLight.lightRange)), 0.0, 1.0);
	//atten *= atten*atten;

	float diffuse = clamp(dot(lightDirVS, normalVS), 0.0, 1.0);
	float specular = 0.2 * pow(clamp(dot(reflectDirVS, lightDirVS), 0.0, 1.0), 10.0);
	vec3 lightOut = attenuation * (diffuse * uLight.lightColor + specular * uLight.lightColor);

	return lightOut;
}
*/

// Encapsulate the various inputs used by the various functions in the shading equation
// We store values in this struct to simplify the integration of alternative implementations
// of the shading terms, outlined in the Readme.MD Appendix.
struct PBRInfo
{
	float NdotL;                  // cos angle between normal and light direction
	float NdotV;                  // cos angle between normal and view direction
	float NdotH;                  // cos angle between normal and half vector
	float LdotH;                  // cos angle between light direction and half vector
	float VdotH;                  // cos angle between view direction and half vector
	float perceptualRoughness;    // roughness value, as authored by the model creator (input to shader)
	float metalness;              // metallic value at the surface
	vec3 reflectance0;            // full reflectance color (normal incidence angle)
	vec3 reflectance90;           // reflectance color at grazing angle
	float alphaRoughness;         // roughness mapped to a more linear change in the roughness (proposed by [2])
	vec3 diffuseColor;            // color contribution from diffuse lighting
	vec3 specularColor;           // color contribution from specular lighting
};

const float M_PI = 3.141592653589793;

// Basic Lambertian diffuse
// Implementation from Lambert's Photometria https://archive.org/details/lambertsphotome00lambgoog
// See also [1], Equation 1
vec3 diffuse(PBRInfo pbrInputs)
{
	return pbrInputs.diffuseColor / M_PI;
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel from [4], Equation 15
vec3 specularReflection(PBRInfo pbrInputs)
{
	return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance0) * pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
}

// This calculates the specular geometric attenuation (aka G()),
// where rougher material will reflect less light back to the viewer.
// This implementation is based on [1] Equation 4, and we adopt their modifications to
// alphaRoughness as input as originally proposed in [2].
float geometricOcclusion(PBRInfo pbrInputs)
{
	float NdotL = pbrInputs.NdotL;
	float NdotV = pbrInputs.NdotV;
	float r = pbrInputs.alphaRoughness;

	float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
	float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
	return attenuationL * attenuationV;
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float microfacetDistribution(PBRInfo pbrInputs)
{
	float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
	float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
	return roughnessSq / (M_PI * f * f);
}

vec3 calcLightCar(
	vec3 baseColor, vec3 emissive, float perceptualRoughness, float metallic, 
	vec3 fragPos, vec3 n, vec3 v, 
	uint clusterIndex, uint pointLightCount, uint spotLightCount
){
	vec3 f0 = vec3(0.04);

	vec3 diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
	diffuseColor *= 1.0 - metallic;
		
	float alphaRoughness = perceptualRoughness * perceptualRoughness;

	vec3 specularColor = mix(f0, baseColor.rgb, metallic);

	// Compute reflectance.
	float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);

	// For typical incident reflectance range (between 4% to 100%) set the grazing reflectance to 100% for typical fresnel effect.
	// For very low reflectance range on highly diffuse objects (below 4%), incrementally reduce grazing reflecance to 0%.
	float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	vec3 specularEnvironmentR0 = specularColor.rgb;
	vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;

	//LightList list = lightLists[clusterIndex];
	uint offset = 0;
	vec3 total = vec3(0.0);

	//for(int i = 0; i < pointLightCount; ++i){
	//	uint lightIndex = list.indices[offset++];
	//	PointLight light = uPointLights.lights[lightIndex];

	for(int i = 0; i < uPointLights.lights.length(); ++i){
		PointLight light = uPointLights.lights[i];

		vec3 lightVec = light.positionRange.xyz - fragPos;
		float distance = length(lightVec);
		if(distance > light.positionRange.w)
			continue;

		vec3 l = normalize(lightVec); // Direction from surface point to light
		vec3 h = normalize(l + v);    // Half vector between both l and v

		float NdotL = clamp(dot(n, l), 0.001, 1.0);
		float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
		float NdotH = clamp(dot(n, h), 0.0, 1.0);
		float LdotH = clamp(dot(l, h), 0.0, 1.0);
		float VdotH = clamp(dot(v, h), 0.0, 1.0);

		PBRInfo pbrInputs = PBRInfo(
			NdotL,
			NdotV,
			NdotH,
			LdotH,
			VdotH,
			perceptualRoughness,
			metallic,
			specularEnvironmentR0,
			specularEnvironmentR90,
			alphaRoughness,
			diffuseColor,
			specularColor
		);

		// Calculate the shading terms for the microfacet specular shading model
		vec3 F = specularReflection(pbrInputs);
		float G = geometricOcclusion(pbrInputs);
		float D = microfacetDistribution(pbrInputs);

		// Calculation of analytical lighting contribution
		vec3 diffuseContrib = (1.0 - F) * diffuse(pbrInputs);
		vec3 specContrib = F * G * D / (4.0 * NdotL * NdotV + 0.001);

		float atte = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);

		// Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
		vec3 color = NdotL * atte * light.colorIntensity.xyz * light.colorIntensity.w * (diffuseContrib + specContrib);

		total += color;
	}

	//for(int i = 0; i < spotLightCount; ++i){
	//	uint lightIndex = list.indices[offset++];
	//	SpotLight light = uSpotLights.lights[lightIndex];

	for(int i = 0; i < uSpotLights.lights.length(); ++i){
		SpotLight light = uSpotLights.lights[i];

		vec3 lightVec = light.positionRange.xyz - fragPos;
		vec3 l = normalize(lightVec); // Direction from surface point to light
		float theta = dot(-l, normalize(light.directionCutoff.xyz));
		if(theta < light.directionCutoff.w)
			continue;

		float distance = length(lightVec);
		if(distance > light.positionRange.w)
			continue;

		vec3 h = normalize(l + v);    // Half vector between both l and v

		float NdotL = clamp(dot(n, l), 0.001, 1.0);
		float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
		float NdotH = clamp(dot(n, h), 0.0, 1.0);
		float LdotH = clamp(dot(l, h), 0.0, 1.0);
		float VdotH = clamp(dot(v, h), 0.0, 1.0);

		PBRInfo pbrInputs = PBRInfo(
			NdotL,
			NdotV,
			NdotH,
			LdotH,
			VdotH,
			perceptualRoughness,
			metallic,
			specularEnvironmentR0,
			specularEnvironmentR90,
			alphaRoughness,
			diffuseColor,
			specularColor
		);

		// Calculate the shading terms for the microfacet specular shading model
		vec3 F = specularReflection(pbrInputs);
		float G = geometricOcclusion(pbrInputs);
		float D = microfacetDistribution(pbrInputs);

		// Calculation of analytical lighting contribution
		vec3 diffuseContrib = (1.0 - F) * diffuse(pbrInputs);
		vec3 specContrib = F * G * D / (4.0 * NdotL * NdotV + 0.001);

		float atte = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);

		// Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
		vec3 color = NdotL * atte * light.colorIntensity.xyz * light.colorIntensity.w * (diffuseContrib + specContrib);

		total += color;
	}

	total += emissive;

	return total;
}

void main(){
	float depth = texture(samplerDepth, inUV).x;
	vec4 rt0 = texture(samplerRT0, inUV);
	vec4 rt1 = texture(samplerRT1, inUV);
	vec4 rt2 = texture(samplerRT2, inUV);

	vec3 normal = decodeNormal(rt0.xy);
	normal = normalize(normal);
	float type = rt1.w;

	float linearDepth = -uCamera.projection[3][2] / (depth + uCamera.projection[2][2]);
	vec3 fragPosVS = inViewRay * linearDepth;
	vec3 fragPos = vec3(uCamera.viewInv * vec4(fragPosVS, 1.0));

	vec3 viewDir = normalize(uCamera.position - fragPos);

	vec3 totalColor;
	vec3 totalLight;

	vec3 uvw;
	uvw.xy = gl_FragCoord.xy / vec2(WIDTH, HEIGHT);
	uvw.z = (-linearDepth - NEAR) / (FAR - NEAR);

	uvec3 clusterIndex = uvec3(uvw * uvec3(CLUSTER_X, CLUSTER_Y, CLUSTER_Z));
	uint index = clusterIndex.z * CLUSTER_Y * CLUSTER_X + clusterIndex.y * CLUSTER_X + clusterIndex.x;

	uint package = texture(clusterData, uvw).x;
	uint spotLightCount = package & 0xFF;
	uint pointLightCount = package >> 8;

	if(type == 0.0){
		vec3 reflectDir = reflect(-viewDir, normal);

		totalColor = rt1.xyz;	
		//totalLight = calcLightCity(fragPos, normal, reflectDir);
		totalLight = vec3(0.0);
	}
	else{
		totalColor = vec3(0.0);

		vec3 baseColor = rt1.xyz;
		vec3 emissive = rt2.xyz;
		float perceptualRoughness, metallic;
		decodeRoughnessMetallic(rt0.w, perceptualRoughness, metallic);

		totalLight = calcLightCar(
			baseColor, emissive, perceptualRoughness, metallic, 
			fragPos, normal, viewDir, 
			index, pointLightCount, spotLightCount
		);
		totalLight = pow(totalLight, vec3(0.4545));
	}

	totalColor += totalLight;

	outColor = vec4(totalColor, 1.0);
	//outColor = vec4(vec3(float(pointLightCount + spotLightCount) / float(MAX_LIST_LENGTH)), 1.0);
	//outColor = vec4(vec3(float(pointLightCount + spotLightCount) / 2.0), 1.0);
}