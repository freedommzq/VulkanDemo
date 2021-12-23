#version 450

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

layout (binding = 5) uniform UBO {
	float exposure;
} ubo;

layout (binding = 1) uniform sampler2D samplerHDR;

vec3 F(vec3 x)
{
	const float A = 0.22f;
	const float B = 0.30f;
	const float C = 0.10f;
	const float D = 0.20f;
	const float E = 0.01f;
	const float F = 0.30f;
 
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 Uncharted2ToneMapping(vec3 color, float adapted_lum)
{
	const float WHITE = 11.2f;
	return F(vec3(1.6f * adapted_lum * color)) / F(vec3(WHITE));
}

vec3 ACESToneMapping(vec3 color, float adapted_lum)
{
	const float A = 2.51f;
	const float B = 0.03f;
	const float C = 2.43f;
	const float D = 0.59f;
	const float E = 0.14f;

	color *= adapted_lum;
	return vec3((color * (A * color + B)) / (color * (C * color + D) + E));
}

void main(){
	vec3 hdrColor = texture(samplerHDR, inUV).xyz;

	// Reinhard
    //vec3 mapped = hdrColor / (hdrColor + vec3(1.0));
	// CryEngine2
    //vec3 mapped = vec3(1.0) - exp(-hdrColor * ubo.exposure);
	// Uncharted2 Flimic
	//vec3 mapped = Uncharted2ToneMapping(hdrColor, ubo.exposure);
	// ACES Filmic
	vec3 mapped = ACESToneMapping(hdrColor, ubo.exposure);

	// 手动gamma编码 (可以通过设置swapchain image的format为SRGB格式让硬件自动完成该计算)
	mapped = pow(mapped, vec3(0.45));

	outFragcolor = vec4(mapped, 1.0);
}