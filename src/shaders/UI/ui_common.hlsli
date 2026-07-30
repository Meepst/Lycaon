#ifndef UI_COMMON_HLSLI
#define UI_COMMON_HLSLI

struct UIConstants{
    float2 scale;
    float2 translate;
    uint texID;
};
[[vk::push_constant]] UIConstants pc;

Texture2D    Textures[]   : register(t0, space1);
SamplerState LinearWrap   : register(s0,space2);
SamplerState LinearClamp  : register(s1,space2);

#endif
