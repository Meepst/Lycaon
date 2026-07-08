#include "restir.hlsli"

#define HISTORY_CAP 64.0
#define REPROJ_TOL 0.01

[numthreads(8,8,1)]
void main(uint3 dtid : SV_DispatchThreadID){
    uint2 px = dtid.xy;
    uint w,h;
    OutputImage.GetDimensions(w,h);

    uint idx = px.y*w+px.x;

    float depth = GBufferDepth.Load(int3(px,0));
    float2 uv = (float2(px)+0.5)/float2(w,h);
    if(depth == 0){
        float3 sky = sampleSky(primaryRayDir(uv));
        OutputImage[px] = float4(linearToSrgb(ACESFilm(sky)),1.0);
        AccumImage[px] = float4(0.0,0.0,0.0,0.0);
        HistPos[px] = float4(0.0,0.0,0.0,0.0);
        CurrReservoirs[idx] = emptyReservoir();
        return;
    }

    Surface surf;

    surf.p = worldFromDepth(uv,depth);

    float4 gb0 = GBuffer0.Load(int3(px,0));
    float4 gb1 = GBuffer1.Load(int3(px,0));
    surf.emissive = GBuffer2.Load(int3(px,0)).rgb;

    surf.albedo = gb0.rgb;
    surf.metallic = gb0.a;
    surf.N = octDecode(gb1.rg);
    surf.roughness = max(gb1.b,0.04);
    float3 V = normalize(cameraPos-surf.p);
    Rng rng = initRng(px,frameInfo.count);

    Reservoir res = IntermediateReservoirs[idx];

    res = spatialReuse(surf,V,res,px,w,h,rng);

    CurrReservoirs[idx] = res;

    float3 d0 = primaryRayDir(uv);
    float3 d1 = primaryRayDir(uv + float2(0.0, 1.0/float(h)));
    float spreadAngle = acos(clamp(dot(d0,d1), -1.0, 1.0));
    float coneWidth   = spreadAngle * length(cameraPos - surf.p);

    float3 direct = surf.emissive;
    float3 indirect = 0.0;
    float3 throughput = 1.f;

    const uint MAX_BOUNCES = 4;
    [loop]
    for(uint b=0;b<MAX_BOUNCES;b++){
        if(b==0){
            direct += throughput*shadeReservoir(surf,V,res);
        }else{
            indirect += throughput*shaderDirectRIS(surf,V,rng,SECONDARY_CANDIDATES);
        }

        if(b+1>=MAX_BOUNCES){
            break;
        }

        float3 kd = surf.albedo*(1.0-surf.metallic);
        throughput *= kd;

        if(b>0){
            float p = max(throughput.r,max(throughput.g,throughput.b));
            if(rndf(rng)>p) break;
            throughput /= p;
        }

        float3 L = sampleCosineHemisphere(surf.N,rnd2(rng));

        RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
        RayDesc ray;
        ray.Origin = surf.p+surf.N*1e-3;
        ray.Direction = L;
        ray.TMin = 1e-3;
        ray.TMax = 1e30;

        query.TraceRayInline(SceneTLAS,RAY_FLAG_FORCE_OPAQUE,0xFF,ray);
        query.Proceed();

        if(query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT){
            indirect += throughput*min(sampleSky(L),20.0)*exposure;
            break;
        }

        coneWidth += spreadAngle*query.CommittedRayT();
        surf = getSurface(query,coneWidth);
        indirect += throughput*surf.emissive;

        V = -L;
    }

    {
        const float maxLum = 8.0;
        float lum = luminance(indirect);
        if(lum>maxLum) indirect*=maxLum/lum;
    }

    if(any(isnan(indirect)) || any(isinf(indirect))) indirect = 0.0;
    if(any(isnan(direct)) || any(isinf(direct))) direct = 0.0;

    float3 primaryP = worldFromDepth(uv,depth);

    float3 histCol = 0.0;
    float histN = 0.0;
    bool valid = (frameInfo.resetHistory == 0);

    if(valid){
        int2 prevPx;
        if(reproject(primaryP,w,h,prevPx)){
            float4 g = PrevHistPos.Load(int3(prevPx,0));
            float d = length(g.xyz-primaryP);
            float tol = REPROJ_TOL*length(cameraPos-primaryP);
            if(g.w > 0.5 && d <= tol){
                float4 c = PrevAccumImage.Load(int3(prevPx,0));
                histCol = c.rgb;
                histN = c.a;
            }else{
                valid = false;
            }
        }else{
            valid = false;
        }
    }

    float3 indirectAvg;
    float newN;
    if(valid){
        float N = min(histN,HISTORY_CAP);
        indirectAvg = (histCol*N+indirect)/(N+1.0);
        newN = N+1.0;
    }else{
        indirectAvg = indirect;
        newN = 1.0;
    }

    AccumImage[px] = float4(indirectAvg,newN);
    HistPos[px] = float4(primaryP,1.0);

    float3 color = direct+indirectAvg;

    OutputImage[px] = float4(linearToSrgb(ACESFilm(color)),1.0);
}
