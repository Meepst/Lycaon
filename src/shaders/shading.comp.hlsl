#include "common.hlsli"

[[vk::push_constant]]
Frame frameInfo;

struct LightSample{
    float3 Lo;
    float3 Li;
    float distance;
};

struct Rng{
    uint state;
};

struct Surface{
    float3 p;
    float3 N;
    float3 albedo;
    float3 emissive;
    float metallic;
    float roughness;
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

float3 shadeDirect(float3 p,float3 N, float3 V,float3 albedo,
    float metallic, float roughness, inout Rng rng, uint shadowTaps){
    float3 col = 0;
    [loop]
    for(uint i=0;i<lightCount;i++){
        Light light = Lights[i];
        LightSample ls = evaluateLight(light,p);
        if(all(ls.Li==0.0)) continue;
        if(dot(N,ls.Lo)<=0.0) continue;

        float3 brdf = evaluateBRDF(N,V,ls.Lo,albedo,metallic,roughness);
        float3 unshadow = brdf*ls.Li;

        if(max(max(unshadow.r,unshadow.g),unshadow.b)<=1e-4) continue;

        float coneCosMax = rsqrt(1.0+(light.radius/ls.distance)*(light.radius/ls.distance));
        float shadow = traceSoftShadow(p,N,ls.Lo,ls.distance,coneCosMax,pcg(rng.state),shadowTaps);
        col += unshadow*shadow;
    }

    return col;
}

Surface getSurface(RayQuery<RAY_FLAG_FORCE_OPAQUE> query){
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

    Material mat = Materials[draw.materialIndex];
    float4 baseColor = mat.diffuseFactor;
    if(mat.albedoTexture>=0){
        baseColor *= Textures[NonUniformResourceIndex(mat.albedoTexture)].SampleLevel(LinearWrap,uv,0);
    }
    surf.albedo = baseColor.rgb;

    if(mat.normalTexture >= 0){
        float3 tanObj = normalize(v0.tangent*w.x+v1.tangent*w.y+v2.tangent*w.z);
        float3 T = normalize(rotateByQuat(tanObj*draw.scale,draw.orientation));
        float biTangentSign = v0.bitangentSign;
        float3 B = cross(surf.N,T)*biTangentSign;
        float3x3 TBN = float3x3(T,B,surf.N);
        float3 ns = Textures[NonUniformResourceIndex(mat.normalTexture)].SampleLevel(LinearWrap,uv,0).xyz;
        ns = ns*2.0-1.0;
        surf.N = normalize(mul(ns,TBN));
    }

    surf.metallic = mat.specularFactor.x;
    float roughness = mat.specularFactor.w;
    roughness = max(roughness,0.04);
    if(mat.specularTexture>=0){
        float4 mr = Textures[NonUniformResourceIndex(mat.specularTexture)].SampleLevel(LinearWrap,uv,0);
        roughness *= mr.g;
        surf.metallic *= mr.b;
    }
    surf.roughness = roughness;

    surf.emissive = mat.emissiveFactor;
    if(mat.emissiveTexture >= 0){
        surf.emissive *= Textures[NonUniformResourceIndex(mat.emissiveTexture)].SampleLevel(LinearWrap,uv,0).rgb;
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
            coneCosMax = cos(light.radius);
        }else{
            coneCosMax = rsqrt(1.0+(light.radius/s.distance)*(light.radius/s.distance));
        }


        float shadow = traceSoftShadow(surf.p,surf.N,s.Lo,s.distance,
            coneCosMax,pcg(rng.state),shadowTaps);
        col += unshadow*shadow;
    }
    return col;
}

[numthreads(8,8,1)]
void main(uint3 dtid : SV_DispatchThreadID){
    uint2 px = dtid.xy;
    uint w,h;
    OutputImage.GetDimensions(w,h);

    float depth = GBufferDepth.Load(int3(px,0));
    if(depth == 0){
        OutputImage[px] = float4(0.f,0.f,0.f,1.f);
        return;
    }

    float2 uv = (float2(px)+0.5)/float2(w,h);

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


    float3 color = 0;

    float3 radiance = surf.emissive;
    float3 throughput = 1.f;

    const uint MAX_BOUNCES = 4;
    [loop]
    for(uint b=0;b<MAX_BOUNCES;b++){
        uint taps = (b==0) ? 8 : 1;
        radiance += throughput*shadeDirect(surf,V,rng,taps);

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
            radiance += throughput*float3(0.53,0.81,0.94); // need to add actual background
            break;
        }

        surf = getSurface(query);
        radiance += throughput*surf.emissive;

        V = -L;
    }

    float3 accumulated;
    if(frameInfo.resetHistory != 0){
        accumulated = radiance;
        AccumImage[px] = float4(radiance,1.0);
    }else{
        float4 hist = AccumImage[px];
        float N = hist.a;
        accumulated = (hist.rgb*N+radiance)/(N+1.0);
        AccumImage[px] = float4(accumulated,min(N+1.0,4096.0));
    }

    OutputImage[px] = float4(linearToSrgb(ACESFilm(accumulated)),1.0);
}
