#include "restir.hlsli"

[numthreads(8,8,1)]
void main(uint3 dtid : SV_DispatchThreadID){
    uint2 px = dtid.xy;
    uint w,h;
    OutputImage.GetDimensions(w,h);
    if(any(dtid.xy >= uint2(w,h))) return;

    uint idx = px.y*w+px.x;

    float depth = GBufferDepth.Load(int3(px,0));
    if(depth==0){
        IntermediateReservoirs[idx] = emptyReservoir();
        return;
    }

    float2 uv = (float2(px)+0.5)/float2(w,h);
    Surface surf;
    surf.p = worldFromDepth(uv, depth);
    float4 gb0 = GBuffer0.Load(int3(px,0));
    float4 gb1 = GBuffer1.Load(int3(px,0));
    surf.emissive = GBuffer2.Load(int3(px,0)).rgb;
    surf.albedo = gb0.rgb;
    surf.metallic = gb0.a;
    surf.N = octDecode(gb1.rg);
    surf.roughness= max(gb1.b, 0.04);

    float3 V = normalize(cameraPos-surf.p);
    Rng rng = initRng(px,frameInfo.count);

    Reservoir res = sampleLightsRIS(surf,V,rng,INITIAL_CANDIDATES);
    reuseVisibility(surf,res);
    res = temporalReuse(surf,V,res,px,w,h,rng);

    IntermediateReservoirs[idx] = res;
}
