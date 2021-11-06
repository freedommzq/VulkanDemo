#version 450

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;

layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec4 outBaseColor;
layout (location = 3) out vec4 outPhysical; // vec4(ao, perceptualRoughness, metallic, 1.0)

layout (set = 1, binding = 0) uniform sampler2D colorMap;
layout (set = 1, binding = 1) uniform sampler2D normalMap;
layout (set = 1, binding = 2) uniform sampler2D physicalDescriptorMap;

layout (push_constant) uniform Material {
	vec4 baseColorFactor;
	int baseColorTextureSet;
	int normalTextureSet;
	int physicalDescriptorTextureSet;
	float metallicFactor;
	float roughnessFactor;
	float alphaMask;
	float alphaMaskCutoff;
} material;

const float c_MinRoughness = 0.04;

#define MANUAL_SRGB 1

vec4 SRGBtoLINEAR(vec4 srgbIn)
{
	#ifdef MANUAL_SRGB
	#ifdef SRGB_FAST_APPROXIMATION
	vec3 linOut = pow(srgbIn.xyz,vec3(2.2));
	#else //SRGB_FAST_APPROXIMATION
	vec3 bLess = step(vec3(0.04045),srgbIn.xyz);
	vec3 linOut = mix( srgbIn.xyz/vec3(12.92), pow((srgbIn.xyz+vec3(0.055))/vec3(1.055),vec3(2.4)), bLess );
	#endif //SRGB_FAST_APPROXIMATION
	return vec4(linOut,srgbIn.w);;
	#else //MANUAL_SRGB
	return srgbIn;
	#endif //MANUAL_SRGB
}

// Find the normal for this fragment, pulling either from a predefined normal map
// or from the interpolated mesh normal and tangent attributes.
vec3 getNormal()
{
	// Perturb normal, see http://www.thetenthplanet.de/archives/1180
	vec3 tangentNormal = texture(normalMap, inUV0).xyz * 2.0 - 1.0;

	vec3 q1 = dFdx(inWorldPos);
	vec3 q2 = dFdy(inWorldPos);
	vec2 st1 = dFdx(inUV0);
	vec2 st2 = dFdy(inUV0);

	vec3 N = normalize(inNormal);
	vec3 T = normalize(q1 * st2.t - q2 * st1.t);
	vec3 B = -normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	return normalize(TBN * tangentNormal);
}

void main()
{
	vec4 baseColor;
	if (material.baseColorTextureSet > -1) {
		baseColor = SRGBtoLINEAR(texture(colorMap, inUV0)) * material.baseColorFactor;
	} else {
		baseColor = material.baseColorFactor;
	}
	if (material.alphaMask == 1.0f && baseColor.a < material.alphaMaskCutoff) {
		discard;
	}

	float perceptualRoughness = material.roughnessFactor;
	float metallic = material.metallicFactor;
	if (material.physicalDescriptorTextureSet > -1) {
		vec4 mrSample = texture(physicalDescriptorMap, inUV0);
		perceptualRoughness = mrSample.g * perceptualRoughness;
		metallic = mrSample.b * metallic;
	} else {
		perceptualRoughness = clamp(perceptualRoughness, c_MinRoughness, 1.0);
		metallic = clamp(metallic, 0.0, 1.0);
	}

	vec3 n = (material.normalTextureSet > -1) ? getNormal() : normalize(inNormal);

	outPosition = vec4(inWorldPos, 1.0);
	outNormal = vec4(n, 1.0);
	outBaseColor = vec4(baseColor.xyz, 1.0);
	outPhysical = vec4(0.0, perceptualRoughness, metallic, 1.0);
}