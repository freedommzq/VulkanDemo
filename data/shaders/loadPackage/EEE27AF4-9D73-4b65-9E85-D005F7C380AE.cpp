std::string testVertShader = R"(
#version 450 compatibility                                 
#extension GL_ARB_bindless_texture : require               
#extension GL_NV_gpu_shader5 : require // uint64_t         
//#extension GL_ARB_gpu_shader5 : require // uint64_t      
//#extension GL_ARB_gpu_shader_int64: require // uint64_t  
in float osg_FrameTime;                                    
out vec2 TexCoord0;                                         
out vec2 TexCoord1;                                         
out vec2 TexCoord2;                                         
out vec2 TexCoord3;                                         
out vec2 TexCoord4;

out vec3 normal;
out vec3 WorldPos;
                                        
flat out int textureIndex0;                                 
flat out int textureIndex1;                                 
flat out int textureIndex2;                                 
flat out int textureIndex3;                                 
flat out int textureIndex4;                                 
void main() {                                             
 
    WorldPos = (gl_Vertex).xyz;                                    
    gl_Position = gl_ModelViewProjectionMatrix*gl_Vertex;        
    TexCoord0 = gl_MultiTexCoord0.xy;
    TexCoord1 = gl_MultiTexCoord1.xy;
    TexCoord2 = gl_MultiTexCoord2.xy;
    TexCoord3 = gl_MultiTexCoord3.xy;
    TexCoord4 = gl_MultiTexCoord4.xy;
	textureIndex0 = int(floor(gl_Color.x/100.0));                          
	textureIndex1 = int(floor(gl_Color.y/100.0));              
	textureIndex2 = int(floor(gl_Color.z/100.0));              
	textureIndex3 = int(floor(gl_Color.w));
	normal = normalize(gl_Normal);           
}                                                          
)";
//we could setup tex to be of type sampler2D, and not have to do
//the type conversion, but I wanted to added code to test if tex
//had a value set. 
//If we get a red cube, we are not getting our handle from our UBO, 
//if we get a black cube, then we are having an issue with the 
//texture handle itself
std::string TestFragShader = R"(
#version 450 compatibility                                    
#extension GL_ARB_bindless_texture : require                  
#extension GL_NV_gpu_shader5 : require // uint64_t            
//#extension GL_ARB_gpu_shader5 : require // uint64_t         
//#extension GL_ARB_gpu_shader_int64: require // uint64_t     
uniform sampler2D TextureId;                                  
in vec2 TexCoord0;                                             
in vec2 TexCoord1;                                             
in vec2 TexCoord2;                                             
in vec2 TexCoord3;                                             
in vec2 TexCoord4;
in vec3 normal;
in vec3 WorldPos;                                     
flat in int textureIndex0;                                     
flat in int textureIndex1;                                     
flat in int textureIndex2;                                     
flat in int textureIndex3; 

uniform float dk;
uniform vec3 ClustersScale;
uniform vec3 ClustersCamPos;
uniform vec3 ClustersBias; 
uniform vec4 gFogColor;

uniform usamplerBuffer Clusters;
uniform samplerBuffer ClustersLights;                                 
                                     
layout (binding = 0, std140) uniform TEXTURE_BLOCK            
{                                                             
    uint64_t      tex[XXX];                                   
};

vec3 getLight(vec3 total_light,uint light_mask,int off,vec4 base,vec3 normal,vec3 refl_vec)
{
	while(light_mask > 0)
	{
		uint i = findLSB(light_mask);
		light_mask &= ~(1<<i);
		//i = 0;
		//vec4 light = Lights[i + off];
		vec4 light = texelFetch(ClustersLights,int((i+off)*3));
		vec3 color = texelFetch(ClustersLights,int((i+off)*3+1)).rgb*base.rgb;
		vec4 dir_cutOff = texelFetch(ClustersLights,int((i+off)*3+2));

		vec3 lVec = light.xyz - WorldPos;			
		vec3 light_vec = normalize(lVec);

		float SpotFactor = dot(-light_vec, normalize(dir_cutOff.xyz));

		if(SpotFactor > dir_cutOff.w)
		{
			float atten = clamp(1.0f - dot(lVec,lVec) * (1.0f / (light.w * light.w )) , 0.0,1.0);

			atten *= atten*atten;
			float diffuse = clamp(dot(light_vec,normal),0.0,1.0);
			float specular = 0.2f * pow(clamp(dot(refl_vec,light_vec),0.0,1.0),10.0f);

			//atten = (atten > 0) ? 0.25f : 0.0f;
			//total_light.r += 0.05f;
			vec3 lightOut = atten * (diffuse * color + specular*color);
			lightOut = lightOut * (1.0 - (1.0 - SpotFactor) * 1.0/(1.0 - dir_cutOff.w));

			total_light += lightOut;	
		}
		//total_light += vec3(0.1,0.0,0.0);
		//test = vec3(light.xyz);
	}
	return total_light;
}

float non_linear_depth_to_linear(float depth, float zNear, float zFar)
{
	float q = zFar / (zFar - zNear);
	return zNear*q / q - depth;	
}

vec4 fogScene(vec4 diffuse,float depth, float intensity)
{
	float z = non_linear_depth_to_linear(depth,0.1,1500.0);
	float fogDensity = intensity; // 0.009
	float LOG2 = 1.442695;
	float fogFactor = exp2(-fogDensity*fogDensity*z*z*LOG2);
	fogFactor = clamp(fogFactor,0.0,1.0);

	vec4 fogColor = mix(gFogColor,diffuse,0.3);
	//vec4 fogColor = mix(vec4(1.0, 1.0, 1.0, 1.0),diffuse,0.1);
	//vec4 fogColor = mix(vec4(FogColor, 1.0), diffuse, 1.0 - FogAlpha);

	vec4 outColor = mix(fogColor,diffuse,fogFactor);
	outColor.a = diffuse.a;

	return outColor;
}
                                                           
void main() {                                                 
    int tIndex0 = (int)(textureIndex0);                         
    int tIndex1 = (int)(textureIndex1);                         
    int tIndex2 = (int)(textureIndex2);                         
    int tIndex3 = (int)(textureIndex3);                         
    sampler2D myText0 = sampler2D(tex[tIndex0]);
    sampler2D myText1 = sampler2D(tex[tIndex1]);
    sampler2D myText2 = sampler2D(tex[tIndex2]);
    
	vec4 base = texture2D(myText0,TexCoord0);
	vec4 blend = texture2D(myText1,TexCoord1);
	vec4 vSunlight = texture2D(myText2,TexCoord2);
	
	if(base.a < 0.6)
	{
		discard;
	}

	float k = 1.0;//dk;
	base *= blend;
	vec4 final_color = base;
	vec4 base0 = base;
	vec4 base1 = vec4(0);
	vec4 base2 = vec4(0);
	      
	final_color = base * (1 - k)/2 + base * vSunlight * k;
	final_color.a = base.a;

    base0 = final_color;
    base.rgb -= base0.rgb;

	if(tIndex3 > 10000) //light_on
	{
		
		sampler2D myText4 = sampler2D(tex[tIndex3 - 10000]);
		vec4 LightMask = texture2D(myText4,TexCoord4);
		
		vec3 view_vec = normalize(ClustersCamPos - WorldPos);
		vec3 refl_vec = reflect(-view_vec,normal);
		vec4 coord = vec4(WorldPos * ClustersScale + ClustersBias,0);
		uvec4 light_mask = texelFetch(Clusters,int(coord.z) * 32*32 + int(coord.y) * 32 + int(coord.x));
		//base0.rgb = base0.rgb * (1.0 - vSunlight.r);
		vec3 total_light = base.xyz*LightMask.r*0.5;

		total_light = getLight(total_light,light_mask.r,0,base,normal,refl_vec)*1.0;
		total_light = getLight(total_light,light_mask.g,32,base,normal,refl_vec)*1.0;
		total_light = getLight(total_light,light_mask.b,64,base,normal,refl_vec)*1.0;
		total_light = getLight(total_light,light_mask.a,96,base,normal,refl_vec)*1.0;
		base1 = vec4(total_light.xyz,0.0);

		k = base1.r;				
		if(k < base1.g)	k = base1.g;
		if(k < base1.b)	k = base1.b;
		if(k > 1)	base1 /= k;	

		base1.a = base.a;
	}

	vec4 out_color = final_color;	//添加顶点颜色
	float shadowTest = 1.0;

	gl_FragData[0] = fogScene(vec4((base0.rgb+base1.rgb+base2.rgb)*1.0,base.a), gl_FragCoord.z/gl_FragCoord.w, 0.006);
	//gl_FragData[0] = vec4(ClustersCamPos,1.0);
    //if (tex[tIndex] == 0) gl_FragColor.r = 1.0;
  	float C = 5;
	float zz = gl_FragCoord.z/gl_FragCoord.w;
	gl_FragDepth = (log(C*zz+1) / log(C*10000.0 + 1));               
}                                                             
)";