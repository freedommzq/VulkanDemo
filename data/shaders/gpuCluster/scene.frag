#version 450

layout (set = 1, binding = 0) uniform sampler2D samplerColorMap;
layout (set = 1, binding = 1) uniform sampler2D samplerNormalMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inViewVec;
layout (location = 4) in vec4 inTangent;
layout (location = 5) in vec3 inPos;

layout (location = 0) out vec4 outFragColor;

layout (constant_id = 0) const bool ALPHA_MASK = false;
layout (constant_id = 1) const float ALPHA_MASK_CUTOFF = 0.0f;
layout (constant_id = 2) const float NEAR = 0.0f;
layout (constant_id = 3) const float FAR = 0.0f;
layout (constant_id = 4) const float WIDTH = 0.0f;
layout (constant_id = 5) const float HEIGHT = 0.0f;
layout (constant_id = 6) const uint CLUSTER_X = 0;
layout (constant_id = 7) const uint CLUSTER_Y = 0;
layout (constant_id = 8) const uint CLUSTER_Z = 0;
const uint MAX_LIST_SIZE = 20;
const uint MAX_LIGHT_SIZE = 1000;

// cluster buffer
struct Cluster{
	uint size;
	uint lights[MAX_LIST_SIZE];
};
layout(set = 0, binding = 1) readonly buffer Clusters{
	Cluster clusters[];
};

// light buffer
struct Light{
	vec4 sphere;
	vec3 color;
};
layout(set = 0, binding = 2) readonly buffer GlobalLights{
	Light lights[];
}globalLights;

layout(set = 0, binding = 3) uniform ClusterCamera{
	mat4 view;
	mat4 projection;
	uint isFreeze;
	uint showCluster;
}clusterCamera;

float linearDepth(float z){
    z = z * 2.0 - 1.0; // Back to NDC 
    return (2.0 * NEAR * FAR) / (FAR + NEAR - z * (FAR - NEAR));
}

void main() 
{
	vec4 color = texture(samplerColorMap, inUV) * vec4(inColor, 1.0);

	if (ALPHA_MASK) {
		if (color.a < ALPHA_MASK_CUTOFF) {
			discard;
		}
	}

	vec3 N = normalize(inNormal);
	vec3 T = normalize(inTangent.xyz);
	vec3 B = cross(inNormal, inTangent.xyz) * inTangent.w;
	mat3 TBN = mat3(T, B, N);
	N = TBN * normalize(texture(samplerNormalMap, inUV).xyz * 2.0 - vec3(1.0));

	vec3 V = normalize(inViewVec);

	// ¼ÆËãclusterIndex
	vec3 uvw;
	if(clusterCamera.showCluster == 1 && clusterCamera.isFreeze == 1){
		vec4 eyePos = clusterCamera.view * vec4(inPos, 1.0);
		vec4 clipPos = clusterCamera.projection * eyePos;
		vec3 ndcPos = clipPos.xyz / clipPos.w;
		uvw.xy = ndcPos.xy * 0.5 + 0.5;
		uvw.z = (eyePos.z - NEAR) / (FAR - NEAR);
	}
	else{
		uvw.xy = gl_FragCoord.xy / vec2(WIDTH, HEIGHT);
		uvw.z = (linearDepth(gl_FragCoord.z) - NEAR) / (FAR - NEAR);	
	}

	uvec3 clusterIndex = uvec3(uvw * uvec3(CLUSTER_X, CLUSTER_Y, CLUSTER_Z));
	uint index = clusterIndex.z * CLUSTER_Y * CLUSTER_X + clusterIndex.y * CLUSTER_X + clusterIndex.x;
	Cluster cluster = clusters[index];

	if(clusterCamera.showCluster == 0){
		vec3 totalDiff = vec3(0.0);
		vec3 totalSpec = vec3(0.0);
		for(uint i = 0; i < cluster.size; ++i){
			if(cluster.lights[i] >= MAX_LIGHT_SIZE)
				continue;

			Light light = globalLights.lights[cluster.lights[i]];

			float distance = length(light.sphere.xyz - inPos);
			if(distance < light.sphere.w){
				vec3 L = normalize(light.sphere.xyz - inPos);
				float diff = max(dot(N, L), 0.0);
				vec3 H = normalize(V + L);
				float spec = pow(max(dot(N, H), 0.0), 32.0);

				float attenuation = 1.0 / (1.0 + 0.1 * distance * distance);
				//float attenuation = 1.0;

				totalDiff += diff * light.color * attenuation;
				totalSpec += spec * light.color * attenuation;
			}
		}

		const vec3 ambient = vec3(0.3);
		outFragColor = vec4((ambient + totalDiff) * color.rgb + totalSpec, color.a);	
	}
	else
		outFragColor = vec4(vec3(0.1 * cluster.size, 0.0, 0.0) + 0.1 * color.rgb, 1.0);
}