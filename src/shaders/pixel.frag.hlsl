#include "common.hlsli"

struct PixelOutput
{
    float4 color : SV_Target0;
	// float4 albedo : SV_Target0;
	// float4 normal : SV_Target1;
	// float4 material : SV_Target2;
	// float4 emissive : SV_Target3;
};

struct LightSample{
    float3 Lo;
    float3 Li;
    float distance;
};

static const float PI     = 3.14159265359;
static const float INV_PI = 1.0 / PI;

float D_GGX(float NdotH, float roughness)
{
	float a  = roughness * roughness;
	float a2 = a * a;
	float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d);
}

float G_SmithGGX(float NdotV, float NdotL, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	float gv = NdotV / (NdotV * (1.0 - k) + k);
	float gl = NdotL / (NdotL * (1.0 - k) + k);
	return gv * gl;
}

float3 F_Schlick(float cosTheta, float3 F0)
{
	float t  = 1.0 - cosTheta;
	float t2 = t * t;
	return F0 + (1.0 - F0) * (t2 * t2 * t);
}

float3 F_SchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	float t  = 1.0 - cosTheta;
	float t2 = t * t;
	float3 maxSpec = max((float3)(1.0 - roughness), F0);
	return F0 + (maxSpec - F0) * (t2 * t2 * t);
}

// ACES filmic (Narkowicz fit)
float3 ACESFilm(float3 x)
{
	float a = 2.51;
	float b = 0.03;
	float c = 2.43;
	float d = 0.59;
	float e = 0.14;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float pointAttenuation(float dist, float range){
    float att = 1.f/max(dist*dist,0.0001);
    if(range>0.0){
        float window = saturate(1.0-pow(dist/range,4.0));
        att *= window*window;
    }
    return att;
}

float traceShadow(float3 pos, float3 N, float3 L, float maxDist){
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    RayDesc ray;
    ray.Origin = pos+N*1e-3;
    ray.Direction = L;
    ray.TMin = 1e-3;
    ray.TMax = maxDist - 2e-3;
    query.TraceRayInline(SceneTLAS, RAY_FLAG_FORCE_OPAQUE| RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,ray);
    query.Proceed();
    return (query.CommittedStatus()==COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
}

LightSample evaluateLight(Light light, float3 pos){
    LightSample samp;
    samp.Lo = float3(0,0,0);
    samp.Li = float3(0,0,0);
    samp.distance = 0.0;

    float3 effective = light.color*light.intensity;

    if(light.type == 1){
        samp.Lo = -normalize(light.direction);
        samp.distance = 1e30;
        samp.Li = effective;
    }else if(light.type == 2){
        float3 toLight = light.position - pos;
        samp.distance = length(toLight);
        samp.Lo = toLight/max(samp.distance,0.0001);
        float att = pointAttenuation(samp.distance,light.range);
        samp.Li = effective*att;
    }else if(light.type == 3){
        float3 toLight = light.position - pos;
        samp.distance = length(toLight);
        samp.Lo = toLight/max(samp.distance,0.0001);
        float distAtt = pointAttenuation(samp.distance,light.range);
        float cosTheta = dot(-samp.Lo,light.direction);
        float coneAtt = smoothstep(light.spotCosOuter,light.spotCosInner,cosTheta);
        samp.Li = effective*distAtt*coneAtt;
    }

    return samp;
}

float3 evaluateBRDF(float3 N, float3 V, float3 L, float3 baseColor,
                    float metallic, float roughness)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    if (NdotL <= 0.0) return float3(0, 0, 0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 F = F_Schlick(VdotH, F0);
    float  D = D_GGX(NdotH, roughness);
    float  G = G_SmithGGX(NdotV, NdotL, roughness);

    float3 specular = (D * F * G) / max(4.0 * NdotV * NdotL, 0.001);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * baseColor * INV_PI;

    return (diffuse + specular) * NdotL;
}

PixelOutput main(VertexOutput input)
{
    Material mat = Materials[input.materialIndex];

    float4 baseColor = mat.diffuseFactor;
    if (mat.albedoTexture >= 0)
        baseColor *= Textures[NonUniformResourceIndex(mat.albedoTexture)].Sample(LinearWrap, input.uv);

    if(mat.alphaMode != 0){
        if(baseColor.a < 0.5) discard;
    }

    // Normal mapping
    float3 N = normalize(input.worldNormal);
    if (mat.normalTexture >= 0)
    {
        float3 T = normalize(input.worldTangent);
        float3 B = cross(N, T) * input.bitangentSign;
        float3x3 TBN = float3x3(T, B, N);

        float3 normalSample = Textures[NonUniformResourceIndex(mat.normalTexture)].Sample(LinearWrap, input.uv).xyz;
        normalSample = normalSample * 2.0 - 1.0;
        N = normalize(mul(normalSample, TBN));
    }

    // Material params
    float metallic  = mat.specularFactor.x;
    float roughness = mat.specularFactor.w;
    roughness = max(roughness, 0.04);  // avoid division issues at zero roughness

    if (mat.specularTexture >= 0)
    {
        float4 mrSample = Textures[NonUniformResourceIndex(mat.specularTexture)].Sample(LinearWrap, input.uv);
        roughness *= mrSample.g;  // glTF: green = roughness
        metallic  *= mrSample.b;  // glTF: blue = metallic
    }

    // Lighting vectors
    float3 V = normalize(cameraPos - input.worldPos);
    float3 color = float3(0,0,0);

    [loop]
    for(uint i=0;i<lightCount;i++){
        Light light = Lights[i];

        if(light.type == 1) continue;

        LightSample samp = evaluateLight(light,input.worldPos);

        if(all(samp.Li==0.0)) continue;

        float NdotL = dot(N,samp.Lo);
        if(NdotL <= 0.0) continue;

        float3 brdf = evaluateBRDF(N,V,samp.Lo,baseColor.rgb,metallic,roughness);

        float3 unshadow = brdf*samp.Li;

        if(max(max(unshadow.r,unshadow.g),unshadow.b)<1e-4) continue;

        float shadow = traceShadow(input.worldPos,N,samp.Lo,samp.distance);

        color += unshadow*shadow;
    }

    // Emissive
    float3 emissive = mat.emissiveFactor;
    if (mat.emissiveTexture >= 0)
        emissive *= Textures[NonUniformResourceIndex(mat.emissiveTexture)].Sample(LinearWrap, input.uv).rgb;
    color += emissive;

    // Ambient (minimal, so things aren't pure black in shadow)
    color += baseColor.rgb * 0.03;

    //Tonemap (simple Reinhard) + gamma
    color = color / (color + 1.0);
    color = pow(color, 1.0 / 2.2);

    if(mat.alphaMode != 0){
        discard;
    }

    PixelOutput output;
    output.color = float4(color,baseColor.a);
    return output;
}
