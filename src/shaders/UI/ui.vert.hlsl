#include "ui_common.hlsli"

struct VSInput{
    [[vk::location(0)]] float2 pos : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 col : COLOR0;
};

struct VSOutput{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};

VSOutput main(VSInput input){
    VSOutput o;
    o.pos = float4(input.pos*pc.scale+pc.translate,0.0,1.0);
    o.uv = input.uv;
    o.col = input.col;
    return o;
}
