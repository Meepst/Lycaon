#include "ui_common.hlsli"

struct PSInput{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};

float4 main(PSInput input) : SV_Target0{
    Texture2D textAtlas = Textures[NonUniformResourceIndex(pc.texID)];
    return input.col*textAtlas.Sample(LinearClamp,input.uv);
}
