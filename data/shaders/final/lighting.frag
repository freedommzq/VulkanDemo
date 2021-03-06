#version 450

layout (binding = 1) uniform sampler2D samplerposition;
layout (binding = 2) uniform sampler2D samplerNormal;
layout (binding = 3) uniform sampler2D samplerAlbedo;
layout (binding = 4) uniform sampler2D samplerEmissive;
layout (binding = 6) uniform sampler2DArray samplerShadowMap;
layout (binding = 7) uniform sampler2D samplerSsaoBlur;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

#define LIGHT_COUNT 3
#define SHADOW_FACTOR 0.25
#define AMBIENT_LIGHT 0.1
#define USE_PCF

struct Light 
{
	vec4 position;
	vec4 target;
	vec4 color;
	mat4 viewMatrix;
};

layout (binding = 5) uniform UBO 
{
	vec4 viewPos;
	Light lights[LIGHT_COUNT];
	int useShadow;
	int useSsao;
} ubo;

float textureProj(vec4 P, float layer, vec2 offset)
{
	float shadow = 1.0;
	vec4 shadowCoord = P / P.w;
	shadowCoord.st = shadowCoord.st * 0.5 + 0.5;
	
	if (shadowCoord.z > -1.0 && shadowCoord.z < 1.0) 
	{
		float dist = texture(samplerShadowMap, vec3(shadowCoord.st + offset, layer)).r;
		if (shadowCoord.w > 0.0 && dist < shadowCoord.z) 
		{
			shadow = SHADOW_FACTOR;
		}
	}
	return shadow;
}

float filterPCF(vec4 sc, float layer)
{
	ivec2 texDim = textureSize(samplerShadowMap, 0).xy;
	float scale = 1.5;
	float dx = scale * 1.0 / float(texDim.x);
	float dy = scale * 1.0 / float(texDim.y);

	float shadowFactor = 0.0;
	int count = 0;
	int range = 1;
	
	for (int x = -range; x <= range; x++)
	{
		for (int y = -range; y <= range; y++)
		{
			shadowFactor += textureProj(sc, layer, vec2(dx*x, dy*y));
			count++;
		}
	
	}
	return shadowFactor / count;
}

float shadow(vec3 fragpos) {
	float shadowFactor = 1.0;
	for(int i = 0; i < LIGHT_COUNT; ++i)
	{
		vec4 shadowClip	= ubo.lights[i].viewMatrix * vec4(fragpos, 1.0);
		
		#ifdef USE_PCF
			shadowFactor *= filterPCF(shadowClip, i);
		#else
			shadowFactor *= textureProj(shadowClip, i, vec2(0.0));
		#endif
	}
	return shadowFactor;
}

void main() 
{
	// Get G-Buffer values
	vec3 fragPos = texture(samplerposition, inUV).rgb;
	vec3 normal = texture(samplerNormal, inUV).rgb;
	vec4 albedo = texture(samplerAlbedo, inUV);
	vec3 emissive = texture(samplerEmissive, inUV).rgb;

	// Ambient part
	float aoFactor = ubo.useSsao == 1 ? texture(samplerSsaoBlur, inUV).x : 1.0;
	vec3 fragcolor  = aoFactor * albedo.rgb * AMBIENT_LIGHT;

	vec3 N = normalize(normal);
		
	for(int i = 0; i < LIGHT_COUNT; ++i)
	{
		// Vector to light
		vec3 L = ubo.lights[i].position.xyz - fragPos;
		// Distance from light to fragment position
		float dist = length(L);
		L = normalize(L);

		// Viewer to fragment
		vec3 V = ubo.viewPos.xyz - fragPos;
		V = normalize(V);

		float lightCosInnerAngle = cos(radians(15.0));
		float lightCosOuterAngle = cos(radians(25.0));
		float lightRange = 100.0;

		// Direction vector from source to target
		vec3 dir = normalize(ubo.lights[i].position.xyz - ubo.lights[i].target.xyz);

		// Dual cone spot light with smooth transition between inner and outer angle
		float cosDir = dot(L, dir);
		float spotEffect = smoothstep(lightCosOuterAngle, lightCosInnerAngle, cosDir);
		float heightAttenuation = smoothstep(lightRange, 0.0f, dist);

		// Diffuse lighting
		float NdotL = max(0.0, dot(N, L));
		vec3 diff = vec3(NdotL);

		// Specular lighting
		vec3 R = reflect(-L, N);
		float NdotR = max(0.0, dot(R, V));
		vec3 spec = vec3(pow(NdotR, 16.0) * albedo.a * 2.5);

		fragcolor += vec3((aoFactor * diff + spec) * spotEffect * heightAttenuation) * ubo.lights[i].color.rgb * albedo.rgb;
	}

	fragcolor += emissive;

	// Shadow calculations in a separate pass
	float shadowFactor = ubo.useShadow == 1 ? shadow(fragPos) : 1.0;

	outFragColor = vec4(fragcolor * shadowFactor, 1.0);
	//outFragColor = vec4(vec3(aoFactor), 1.0);
}