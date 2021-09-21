#version 450

layout (input_attachment_index = 0, binding = 3) uniform subpassInput samplerposition;
layout (input_attachment_index = 1, binding = 4) uniform subpassInput samplerNormal;
layout (input_attachment_index = 2, binding = 5) uniform subpassInput samplerAlbedo;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

struct Light {
	vec4 position;
	vec3 color;
	float radius;
};

layout (binding = 6) uniform UBO 
{
	Light lights[6];
	vec4 viewPos;
	int displayDebugTarget;
} ubo;

void main() 
{
	// Get G-Buffer values
	vec3 fragPos = subpassLoad(samplerposition).rgb;
	vec3 normal = subpassLoad(samplerNormal).rgb;
	vec4 albedo = subpassLoad(samplerAlbedo);
	
	// Debug display
	if (ubo.displayDebugTarget > 0) {
		switch (ubo.displayDebugTarget) {
			case 1: 
				outFragcolor.rgb = fragPos;
				break;
			case 2: 
				outFragcolor.rgb = normal;
				break;
			case 3: 
				outFragcolor.rgb = albedo.rgb;
				break;
			case 4: 
				outFragcolor.rgb = albedo.aaa;
				break;
		}		
		outFragcolor.a = 1.0;
		return;
	}

	// Render-target composition

	#define lightCount 6
	#define ambient 0.0
	
	// Ambient part
	vec3 fragcolor  = albedo.rgb * ambient;
	
	for(int i = 0; i < lightCount; ++i)
	{
		// Vector to light
		vec3 L = ubo.lights[i].position.xyz - fragPos;
		// Distance from light to fragment position
		float dist = length(L);

		// Viewer to fragment
		vec3 V = ubo.viewPos.xyz - fragPos;
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
   
  outFragcolor = vec4(fragcolor, 1.0);	
}