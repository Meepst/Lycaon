#include "common.hlsli"

struct PixelOutput
{
	float4 color : SV_Target0;
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

PixelOutput main(VertexOutput input)
{
    Material mat = Materials[input.materialIndex];

    // Base color
    float4 baseColor = mat.diffuseFactor;
    if (mat.albedoTexture >= 0)
        baseColor *= Textures[mat.albedoTexture].Sample(LinearWrap, input.uv);

    // Normal mapping
    float3 N = normalize(input.worldNormal);
    if (mat.normalTexture >= 0)
    {
        float3 T = normalize(input.worldTangent);
        float3 B = cross(N, T) * input.bitangentSign;
        float3x3 TBN = float3x3(T, B, N);

        float3 normalSample = Textures[mat.normalTexture].Sample(LinearWrap, input.uv).xyz;
        normalSample = normalSample * 2.0 - 1.0;
        N = normalize(mul(normalSample, TBN));
    }

    // Material params
    float metallic  = mat.specularFactor.x;  // you packed F0 here, recover metallic
    float roughness = mat.specularFactor.w;
    roughness = max(roughness, 0.04);  // avoid division issues at zero roughness

    if (mat.specularTexture >= 0)
    {
        float4 mrSample = Textures[mat.specularTexture].Sample(LinearWrap, input.uv);
        roughness *= mrSample.g;  // glTF: green = roughness
        metallic  *= mrSample.b;  // glTF: blue = metallic
    }

    // Lighting vectors
    float3 V = normalize(cameraPos - input.worldPos);
    float3 L = sunDirection;
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // PBR (Cook-Torrance)
    // F0: dielectric = 0.04, metallic = base color
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);

    // Fresnel (Schlick)
    float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    // Distribution (GGX)
    float a  = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159265 * denom * denom);

    // Geometry (Smith GGX)
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G1V = NdotV / (NdotV * (1.0 - k) + k);
    float G1L = NdotL / (NdotL * (1.0 - k) + k);
    float G = G1V * G1L;

    // Specular BRDF
    float3 specular = (D * F * G) / (4.0 * NdotV * NdotL + 0.001);

    // Diffuse (Lambert, energy-conserving)
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * baseColor.rgb / 3.14159265;

    // Combine
    float3 color = (diffuse + specular) * sunColor * sunIntensity * NdotL;

    // Emissive
    float3 emissive = mat.emissiveFactor;
    if (mat.emissiveTexture >= 0)
        emissive *= Textures[mat.emissiveTexture].Sample(LinearWrap, input.uv).rgb;
    color += emissive;

    // Ambient (minimal, so things aren't pure black in shadow)
    color += baseColor.rgb * 0.03;

    //Tonemap (simple Reinhard) + gamma
    color = color / (color + 1.0);
    color = pow(color, 1.0 / 2.2);

    PixelOutput output;
    output.color = float4(color, baseColor.a);
    return output;
}
