#include "common.hlsli"

struct PixelOutput
{
    float4 gBuffer0 : SV_Target0;
    float4 gBuffer1 : SV_Target1;
    float3 gBuffer2 : SV_Target2;
};

// for encoding UVs
float2 octEncode2(float2 v){
    float2 sign = float2(v.x >= 0.0 ? 1.0 : -1.0,
        v.y >= 0.0 ? 1.0 : -1.0);
    return (1.0-abs(v.yx))*sign;
}

PixelOutput main(VertexOutput input)
{
    Material mat = Materials[input.materialIndex];

    float4 baseColor = mat.diffuseFactor;
    if (mat.albedoTexture >= 0)
        baseColor *= Textures[NonUniformResourceIndex(mat.albedoTexture)].Sample(LinearWrap, input.uv);

    if(mat.alphaMode != 0 && baseColor.a < 0.5){
        discard;
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


    // Emissive
    float3 emissive = mat.emissiveFactor;
    if (mat.emissiveTexture >= 0)
        emissive *= Textures[NonUniformResourceIndex(mat.emissiveTexture)].Sample(LinearWrap, input.uv).rgb;

    float2 octN = octEncode(N);
    float matFlags = 0; // will use for alternative brdfs later

    PixelOutput output;
    output.gBuffer0 = float4(baseColor.rgb,metallic);
    output.gBuffer1 = float4(octN,roughness,matFlags/3.0);
    output.gBuffer2 = emissive;
    return output;
}
