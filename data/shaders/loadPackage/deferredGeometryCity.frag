#version 450

layout (constant_id = 0) const uint TEXTURE_COUNT = 50;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inUV0;
layout (location = 2) in vec2 inUV1;
layout (location = 3) in vec2 inUV2;
layout (location = 4) in vec2 inUV3;
layout (location = 5) flat in uint inTexIndex0;
layout (location = 6) flat in uint inTexIndex1;
layout (location = 7) flat in uint inTexIndex2;
layout (location = 8) flat in uint inTexIndex3;

layout (location = 0) out vec4 outRT0; // (Normal.x, Normal.y, EmissiveFactor, Roughness + Metallic)
layout (location = 1) out vec4 outRT1; // (BaseColor.rgb/Diffuse.rgb, ShadingModelType)
layout (location = 2) out vec4 outRT2;

layout (set = 1, binding = 0) uniform sampler2D textures[TEXTURE_COUNT];

float encodeRoughnessMetallic(float r, float m){
	return r * 100.0 + m / 256.0;
}

vec2 encodeNormal(vec3 n){
	n = normalize(n);
	float p = sqrt(n.z * 2 + 2);
	return n.xy / p;
}

void main() 
{
	vec4 color = texture(textures[inTexIndex0], inUV0);
	vec4 blend = texture(textures[inTexIndex1], inUV1);
	vec4 vSunLight = texture(textures[inTexIndex2], inUV2);

	if(color.a < 0.6) discard;

	vec3 base = color.rgb * blend.rgb;
	float k = 0.2;
	vec3 base0 = base * (1 - k)/2 + base * vSunLight.rgb * k;

	vec3 base1 = vec3(0.0);
	/*
	if(inTexIndex3 > 0) { //light_on
		vec4 LightMask = texture(textures[inTexIndex3], inUV3);

		base -= base0;
		base1 = base * LightMask.r * 0.5; // Baked Light
	}
	*/

	float perceptualRoughness = 1.0;
	float metallic = 0.0;
	float roughnessMetallic = encodeRoughnessMetallic(perceptualRoughness, metallic);

	outRT0 = vec4(encodeNormal(inNormal), 1.0, roughnessMetallic);
	outRT1 = vec4(base0 + base1, 0.0); // 0.0 - City
	outRT2 = vec4(vec3(0.0), 1.0);

	//float C = 5;
	//float zz = gl_FragCoord.z / gl_FragCoord.w;
	//gl_FragDepth = (log(C * zz + 1) / log(C * 10000.0 + 1));
}