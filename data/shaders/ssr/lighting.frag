#version 450

layout(early_fragment_tests) in;

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec3 inTangent;

layout (location = 0) out vec4 outNormal;
layout (location = 1) out vec4 outDirectLight;


struct Light {
	vec4 position;
	vec3 color;
	float radius;
};

layout (binding = 4) uniform UBO 
{
	Light lights[6];
	mat4 view;
} ubo;

layout (binding = 1) uniform sampler2D samplerColorMap;
layout (binding = 2) uniform sampler2D samplerNormalMap;


void main() 
{
	// Get G-Buffer values
	vec3 fragPos = inWorldPos;

	// Calculate normal in tangent space
	vec3 N = normalize(inNormal);
	vec3 T = normalize(inTangent);
	vec3 B = cross(N, T);
	mat3 TBN = mat3(T, B, N);
	//vec3 normal = TBN * normalize(texture(samplerNormalMap, inUV).xyz * 2.0 - vec3(1.0));
	vec3 normal = N;

	vec4 albedo = texture(samplerColorMap, inUV);



	#define lightCount 6
	#define ambient 0.0
	
	// Ambient part
	vec3 fragcolor  = albedo.rgb * ambient;
	
	// Direct light calculate in view space
	for(int i = 0; i < lightCount; ++i)
	{
		// Vector to light
		vec3 lightPos = ubo.lights[i].position.xyz; // World space light position
		vec3 L = (ubo.view * vec4(lightPos, 1.0)).xyz - fragPos;
		// Distance from light to fragment position
		float dist = length(L);

		// Viewer to fragment
		vec3 V = -fragPos;
		V = normalize(V);
		
		//if(dist < ubo.lights[i].radius)
		{
			// Light to fragment
			L = normalize(L);

			// Attenuation
			float atten = ubo.lights[i].radius / (pow(dist, 2.0) + 1.0);

			// Diffuse part
			vec3 N = normalize(normal);
			float NdotL = max(0.0, dot(N, L));
			vec3 diff = ubo.lights[i].color * albedo.rgb * NdotL * atten;

			// Specular part
			// Specular map values are stored in alpha of albedo mrt
			vec3 R = reflect(-L, N);
			float NdotR = max(0.0, dot(R, V));
			vec3 spec = ubo.lights[i].color * albedo.a * pow(NdotR, 16.0) * atten;

			fragcolor += diff + spec;	
		}	
	}    	
	
	outNormal = vec4(normal, 1.0);
	outDirectLight = vec4(fragcolor, 1.0);
	//outDirectLight = albedo;
}