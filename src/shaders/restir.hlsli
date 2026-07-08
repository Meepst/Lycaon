#ifndef RESTIR_HLSLI
#define RESTIR_HLSLI

#include "common.hlsli"

#define INITIAL_CANDIDATES 10
#define SECONDARY_CANDIDATES 8
#define SPATIAL_NEIGHBORS 5
#define SPATIAL_RADIUS 30.0
#define M_CLAMP 20.0
#define NORMAL_THRESHOLD 0.906
#define DEPTH_THRESHOLD 0.01

static const float PI = 3.14159265359;
static const float INV_PI = 0.31830989;
static const float INV_2PI = 0.15915494;

static const float exposure = 0.003;
static const float skyIntensity = 0.55;

[[vk::push_constant]]
Frame frameInfo;

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

float luminance(float3 c){
    return dot(c,float3(0.2126,0.7152,0.0722));
}

float3 sampleSky(float3 dir){
    dir = normalize(dir);
    float2 uv;
    uv.x = atan2(dir.z,dir.x)*INV_2PI+0.5;
    uv.y = acos(clamp(dir.y,-1.0,1.0))*INV_PI;
    return Skybox.SampleLevel(LinearClamp,uv,0).rgb*skyIntensity;
}

float3 primaryRayDir(float2 uv){
    float2 ndc = float2(uv.x,1.0-uv.y)*2.0-1.0;
    float4 far = mul(invViewProj,float4(ndc,1.0,1.0));
    return normalize(far.xyz/far.w-cameraPos);
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

    float3 effective = light.color*light.intensity*INV_PI;

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

float3 worldFromDepth(float2 uv, float depth){
    float2 ndc = float2(uv.x,1.0-uv.y)*2.0-1.0;
    float4 clip = float4(ndc,depth,1.0);
    float4 world = mul(invViewProj,clip);
    return world.xyz / world.w;
}

float3 linearToSrgb(float3 c){
    float3 lo = c*12.92;
    float3 hi = 1.055*pow(c,1.0/2.4)-0.055;
    return lerp(hi,lo,step(c,0.0031308));
}

uint hashSeed(uint2 px, uint frame){
    uint h = px.x * 73856093u ^ px.y * 19349663u ^ frame * 83492791u;
    return h;
}

float2 hash2(uint s) {
    s ^= s >> 16; s *= 0x7feb352du; s ^= s >> 15;
    uint t = s * 0x846ca68bu; t ^= t >> 16;
    return float2(s & 0xffff, t & 0xffff) / 65535.0;
}

uint pcg(inout uint s){
    s=s*747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28) + 4u)) ^ s) * 277803737u;
    return (w>>22)^w;
}

float rndf(inout Rng r){
    return pcg(r.state)*(1.0/4294967296.0);
}

float2 rnd2(inout Rng r){
    return float2(rndf(r),rndf(r));
}

Rng initRng(uint2 px, uint frame){
    Rng r;
    r.state = hashSeed(px,frame);
    return r;
}

float3 sampleCone(float3 Lo, float cosMax, float2 u){
    float cosT = lerp(1.0, cosMax, u.x);
    float sinT = sqrt(saturate(1.0 - cosT * cosT));
    float phi  = 6.2831853 * u.y;
    float3 up  = abs(Lo.y) < 0.99 ? float3(0,1,0) : float3(1,0,0);
    float3 t   = normalize(cross(up, Lo));
    float3 b   = cross(Lo, t);
    return normalize(Lo * cosT + (t * cos(phi) + b * sin(phi)) * sinT);
}

float traceSoftShadow(float3 p, float3 N, float3 Lo,float distL,
    float coneCosMax, uint seed,uint taps){
    float vis = 0.0;
    for(uint s=0;s<taps;s++){
        Rng r;
        r.state = seed+s;
        float3 dir = sampleCone(Lo,coneCosMax,rnd2(r));
        vis += traceShadow(p,N,dir,distL);
    }
    return vis/taps;
}

void buildTBN(float3 N, out float3 T, out float3 B){
    float3 up = abs(N.y)<0.99?float3(0,1,0):float3(1,0,0);
    T = normalize(cross(up,N));
    B = cross(N,T);
}

float3 sampleCosineHemisphere(float3 N, float2 u){
    float r = sqrt(u.x);
    float phi = 6.2831853*u.y;
    float3 T,B;
    buildTBN(N,T,B);
    return normalize(T*(r*cos(phi))+B*(r*sin(phi))+N*sqrt(saturate(1.0-u.x)));
}

Surface getSurface(RayQuery<RAY_FLAG_FORCE_OPAQUE> query,float coneWidth){
    Surface surf;
    uint drawIndex = query.CommittedInstanceID();
    MeshDraw draw = Draws[drawIndex];
    Mesh mesh = Meshes[draw.meshIndex];

    uint prim = query.CommittedPrimitiveIndex();
    uint ib = mesh.lods[0].indexOffset;
    uint3 tri = indexBuffer.Load3((ib+prim*3)*4);
    UnpackedVertex v0 = loadVertex(mesh.vertexOffset+tri.x);
    UnpackedVertex v1 = loadVertex(mesh.vertexOffset+tri.y);
    UnpackedVertex v2 = loadVertex(mesh.vertexOffset+tri.z);

    float2 bc = query.CommittedTriangleBarycentrics();
    float3 w = float3(1-bc.x-bc.y,bc.x,bc.y);

    float3 nObj = normalize(v0.normal*w.x + v1.normal*w.y + v2.normal*w.z);

    surf.N = normalize(rotateByQuat(nObj/draw.scale,draw.orientation));

    float2 uv = v0.uv*w.x+v1.uv*w.y+v2.uv*w.z;

    float3 e0 = (v1.position-v0.position)*draw.scale;
    float3 e1 = (v2.position-v0.position)*draw.scale;
    float T_a = length(cross(e0,e1));

    float P_a = abs((v1.uv.x-v0.uv.x)*(v2.uv.y-v0.uv.y)
        -(v2.uv.x-v0.uv.x)*(v1.uv.y-v0.uv.y));

    float3 rd = query.WorldRayDirection();
    float lodBase = 0.5*log2(max(T_a,1e-12)/max(P_a,1e-12))
        +log2(max(coneWidth,1e-12))-log2(max(abs(dot(rd,surf.N)),1e-4));

    Material mat = Materials[draw.materialIndex];
    float4 baseColor = mat.diffuseFactor;
    if(mat.albedoTexture>=0){
        Texture2D t = Textures[NonUniformResourceIndex(mat.albedoTexture)];
        uint tw,th;
        t.GetDimensions(tw,th);
        float lod = lodBase+0.5*log2(float(tw)*float(th));
        baseColor *= t.SampleLevel(LinearWrap,uv,lod);
    }
    surf.albedo = baseColor.rgb;

    if(mat.normalTexture >= 0){
        float3 tanObj = normalize(v0.tangent*w.x+v1.tangent*w.y+v2.tangent*w.z);
        float3 T = normalize(rotateByQuat(tanObj*draw.scale,draw.orientation));
        float biTangentSign = v0.bitangentSign;
        float3 B = cross(surf.N,T)*biTangentSign;
        float3x3 TBN = float3x3(T,B,surf.N);
        Texture2D t = Textures[NonUniformResourceIndex(mat.normalTexture)];
        uint tw,th;
        t.GetDimensions(tw,th);
        float lod = lodBase+0.5*log2(float(tw)*float(th));
        float3 ns = t.SampleLevel(LinearWrap,uv,lod).xyz;
        ns = ns*2.0-1.0;
        surf.N = normalize(mul(ns,TBN));
    }

    surf.metallic = mat.specularFactor.x;
    float roughness = mat.specularFactor.w;
    roughness = max(roughness,0.04);
    if(mat.specularTexture>=0){
        Texture2D t = Textures[NonUniformResourceIndex(mat.specularTexture)];
        uint tw,th;
        t.GetDimensions(tw,th);
        float lod = lodBase+0.5*log2(float(tw)*float(th));
        float4 mr = t.SampleLevel(LinearWrap,uv,lod);
        roughness *= mr.g;
        surf.metallic *= mr.b;
    }
    surf.roughness = roughness;

    surf.emissive = mat.emissiveFactor;
    if(mat.emissiveTexture >= 0){
        Texture2D t = Textures[NonUniformResourceIndex(mat.emissiveTexture)];
        uint tw,th;
        t.GetDimensions(tw,th);
        float lod = lodBase+0.5*log2(float(tw)*float(th));
        surf.emissive *= t.SampleLevel(LinearWrap,uv,lod).rgb;
    }

    surf.p = query.WorldRayOrigin()+query.CommittedRayT()*query.WorldRayDirection();

    return surf;
}

float3 shadeDirect(Surface surf, float3 V, inout Rng rng, uint shadowTaps){
    float3 col = 0;
    [loop]
    for(uint i=0;i<lightCount;i++){
        Light light = Lights[i];
        LightSample s = evaluateLight(light,surf.p);

        if(all(s.Li==0.0)) continue;

        float NdotL = dot(surf.N,s.Lo);
        if(NdotL <= 0.0) continue;

        float3 brdf = evaluateBRDF(surf.N,V,s.Lo,surf.albedo,surf.metallic,surf.roughness);
        float3 unshadow = brdf*s.Li;
        if(max(max(unshadow.r,unshadow.g),unshadow.b)<1e-4) continue;

        float coneCosMax;
        if(light.type == 1){
            coneCosMax = cos(light.radius/PI);
        }else{
            coneCosMax = rsqrt(1.0+(light.radius/s.distance)*(light.radius/s.distance));
        }


        float shadow = traceSoftShadow(surf.p,surf.N,s.Lo,s.distance,
            coneCosMax,pcg(rng.state),shadowTaps);
        col += unshadow*shadow;
    }
    return col;
}

void loadFromGBuffer(uint2 px, out Surface surf, out float3 V){
    float depth = GBufferDepth.Load(int3(px,0));
    uint w,h;
    OutputImage.GetDimensions(w,h);
    float2 uv = (float2(px)+0.5)/float2(w,h);
    surf.p = worldFromDepth(uv,depth);
    float4 gb0 = GBuffer0.Load(int3(px,0));
    float4 gb1 = GBuffer1.Load(int3(px,0));
    surf.albedo = gb0.rgb;
    surf.metallic = gb0.a;
    surf.N = octDecode(gb1.rg);
    surf.roughness = max(gb1.b,0.04);
    surf.emissive = 0;
    V = normalize(cameraPos-surf.p);
}

Reservoir emptyReservoir(){
    Reservoir res;
    res.lightID = 0;
    res.wSum = 0.0;
    res.M = 0.0;
    res.W = 0.0;
    res.pos = float3(0.f,0.f,0.f);
    res.normal = float3(0.f,0.f,0.f);
    return res;
}

bool updateReservoir(inout Reservoir res, uint lightID, float w, inout Rng rng){
    res.wSum += w;
    res.M += 1.0;
    bool take = (res.wSum > 0.0) && (rndf(rng) < w / res.wSum);
    if(take) res.lightID = lightID;
    return take;
}

float targetPdf(Surface surf, float3 V, uint lightID){
    Light light = Lights[lightID];
    LightSample s = evaluateLight(light,surf.p);
    if(all(s.Li == 0.0)) return 0.0;
    if(dot(surf.N,s.Lo)<=0.0) return 0.0;
    float3 brdf = evaluateBRDF(surf.N,V,s.Lo,surf.albedo,
        surf.metallic,surf.roughness);
    return luminance(brdf*s.Li);
}

Reservoir sampleLightsRIS(Surface surf, float3 V, inout Rng rng, uint numCandidates){
    Reservoir res = emptyReservoir();
    float invSourcePdf = float(lightCount);

    for(uint i=0;i<numCandidates;i++){
        uint idx = min(uint(rndf(rng)*lightCount),lightCount-1u);
        float pHat = targetPdf(surf,V,idx);
        updateReservoir(res,idx,pHat*invSourcePdf,rng);
    }
    float pHatY = targetPdf(surf,V,res.lightID);
    res.W = (pHatY > 0.0 && res.M > 0.0) ? (res.wSum / (res.M*pHatY)) : 0.0;
    return res;
}

void reuseVisibility(Surface surf, inout Reservoir res){
    if(res.W <= 0.0){
        return;
    }
    LightSample s = evaluateLight(Lights[res.lightID],surf.p);
    if(traceShadow(surf.p,surf.N,s.Lo,s.distance)<=0.0){
        res.W = 0.0;
    }
}

Reservoir combineReservoirs(Surface surf, float3 V, Reservoir a, Reservoir b, inout Rng rng){
    Reservoir res = emptyReservoir();
    res.lightID = a.lightID;
    updateReservoir(res,a.lightID,targetPdf(surf,V,a.lightID)*a.W*a.M,rng);
    updateReservoir(res,b.lightID,targetPdf(surf,V,b.lightID)*b.W*b.M,rng);
    res.M = a.M+b.M;
    float pHatY = targetPdf(surf,V,res.lightID);
    res.W = (pHatY > 1e-6) ? (res.wSum / (res.M*pHatY)) : 0.0;
    return res;
}

bool reproject(float3 worldPos, uint w, uint h, out int2 prevPx){
    float4 clip = mul(prevViewProj, float4(worldPos,1.0));
    if(clip.w <= 0.0){
        prevPx = int2(-1,-1);
        return false;
    }
    float2 ndc = clip.xy / clip.w;
    float2 uv = float2(ndc.x*0.5+0.5,0.5-ndc.y*0.5);
    prevPx = int2(uv*float2(w,h));
    return all(prevPx >= 0) && all(prevPx < int2(w,h));
}

bool reservoirGeometryValid(Surface curr, Reservoir prev){
    if(prev.M <= 0.0) return false;
    if(dot(curr.N,prev.normal) < NORMAL_THRESHOLD) return false;
    float planeDist = abs(dot(prev.pos-curr.p,curr.N));
    return planeDist <= DEPTH_THRESHOLD*length(cameraPos-curr.p);
}

Reservoir temporalReuse(Surface surf, float3 V, Reservoir cur, uint2 px,
    uint w, uint h, inout Rng rng){
    if(frameInfo.resetHistory != 0) return cur;
    int2 prevPx;
    if(!reproject(surf.p,w,h,prevPx)) return cur;
    Reservoir prev = PrevReservoirs[prevPx.y*w+prevPx.x];
    if(!reservoirGeometryValid(surf,prev)) return cur;

    prev.M = min(prev.M,M_CLAMP*cur.M);
    return combineReservoirs(surf,V,cur,prev,rng);
}

bool reservoirGeometrySimilar(Surface a, Reservoir bRes){
    if(bRes.M <= 0.0) return false;
    if(dot(a.N,bRes.normal) < NORMAL_THRESHOLD) return false;
    float planeDist = abs(dot(bRes.pos-a.p,a.N));
    return planeDist <= DEPTH_THRESHOLD*length(cameraPos-a.p);
}

// biased version
Reservoir spatialReuse(Surface surf, float3 V, Reservoir centerRes, uint2 px,
    uint w, uint h, inout Rng rng){
    Reservoir s = emptyReservoir();
    s.pos = surf.p;
    s.normal = surf.N;

    float Msum = 0.0;
    updateReservoir(s,centerRes.lightID,targetPdf(surf,V,centerRes.lightID)
        *centerRes.W*centerRes.M,rng);
    Msum += centerRes.M;

    for(uint n = 0;n<SPATIAL_NEIGHBORS;n++){
        int2 q = int2(px)+int2((rnd2(rng)*2.0-1.0)*SPATIAL_RADIUS);
        if(any(q<0) || any(q>=int2(w,h)) || all(q==int2(px))) continue;
        Reservoir nb = IntermediateReservoirs[q.y*w+q.x];
        if(!reservoirGeometrySimilar(surf,nb)) continue;
        updateReservoir(s,nb.lightID,targetPdf(surf,V,nb.lightID)
            *nb.W*nb.M,rng);
        Msum += nb.M;
    }

    s.M = Msum;
    float pHatY = targetPdf(surf,V,s.lightID);
    s.W = (pHatY > 0.0 && s.M > 0.0) ? (s.wSum / (s.M*pHatY)) : 0.0;
    return s;
}

float3 shadeReservoir(Surface surf, float3 V, Reservoir res){
    if(res.W <= 0.0) return float3(0.f,0.f,0.f);
    Light light = Lights[res.lightID];
    LightSample s = evaluateLight(light,surf.p);
    if(dot(surf.N,s.Lo) <= 0.0) return float3(0.f,0.f,0.f);

    float3 brdf = evaluateBRDF(surf.N,V,s.Lo,surf.albedo,surf.metallic,surf.roughness);
    float coneCosMax = (light.type == 1) ? cos(light.radius / PI) : rsqrt(1.0+(light.radius/s.distance)*(light.radius/s.distance));
    float shadow = traceSoftShadow(surf.p,surf.N,s.Lo,s.distance,coneCosMax,0u,1u);
    return brdf*s.Li*shadow*res.W;
}

float3 shaderDirectRIS(Surface surf, float3 V, inout Rng rng, uint numCandidates){
    if(lightCount == 0u) return float3(0.f,0.f,0.f);
    Reservoir res = sampleLightsRIS(surf,V,rng,numCandidates);
    if(res.W <= 0.0) return float3(0.f,0.f,0.f);
    Light light = Lights[res.lightID];
    LightSample s = evaluateLight(light,surf.p);
    if(dot(surf.N,s.Lo) <= 0.0) return float3(0.f,0.f,0.f);
    float3 brdf = evaluateBRDF(surf.N,V,s.Lo,surf.albedo,surf.metallic,surf.roughness);
    float shadow = traceShadow(surf.p,surf.N,s.Lo,s.distance);
    return brdf*s.Li*shadow*res.W;
}


#endif
