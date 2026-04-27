#ifndef COMMON_HLSLI
#define COMMON_HLSLI

struct MeshLod
{
	uint  indexOffset;
	uint  indexCount;
	uint  meshletOffset;
	uint  meshletCount;
	float error;
};

struct Mesh
{
	float3  center;
	float   radius;
	uint    vertexOffset;
	uint    vertexCount;
	uint    ommIndexData;
	uint    ommIndexBase;
	uint    lodCount;
	uint3   _pad;
	MeshLod lods[8];
};

struct Material
{
	int    albedoTexture;
	int    normalTexture;
	int    specularTexture;
	int    emissiveTexture;
	float4 diffuseFactor;
	float4 specularFactor;
	float3 emissiveFactor;
	uint   alphaMode;
};

struct MeshDraw
{
	float3 position;
	float  scale;
	float4 orientation;
	uint   meshIndex;
	uint   meshletVisibilityOffset;
	uint   postPass;
	uint   materialIndex;
};

struct Light{
    float3 position;
    uint   type;
    float3 color;
    float  intensity;
    float3 direction;
    float  spotCosInner;
    float  spotCosOuter;
    float  range;
    float  _pad0;
    float  _pad1;
};

struct AliasEntry{
    float probability;
    uint alias;
    float pdf;
    uint _pad;
};

struct Globals
{
	float4x4 viewProj;
	float4x4 view;
	float4x4 proj;
	float4x4 invViewProj;
	float3   cameraPos;
	float    _pad0;
	float    _pad1;
	uint     lightCount;
	float2   screenSize;
	float    nearPlane;
	float    farPlane;
};

struct VertexOutput
{
    float4 clipPos       : SV_Position;
    [[vk::location(0)]] float3 worldPos      : POSITION;
    [[vk::location(1)]] float3 worldNormal   : NORMAL;
    [[vk::location(2)]] float3 worldTangent  : TANGENT;
    [[vk::location(3)]] float  bitangentSign : BITANGENT_SIGN;
    [[vk::location(4)]] float2 uv            : TEXCOORD0;
    [[vk::location(5)]] nointerpolation uint materialIndex : MATERIAL;
};

StructuredBuffer<Globals>  GlobalsBuf    : register(t0,space0);
ByteAddressBuffer          VertexBuffer  : register(t1,space0);
StructuredBuffer<uint>     MeshletData   : register(t2,space0);
ByteAddressBuffer          MeshletBuffer : register(t3,space0);
StructuredBuffer<Mesh>     Meshes        : register(t4,space0);
StructuredBuffer<MeshDraw> Draws         : register(t5,space0);
StructuredBuffer<Material> Materials     : register(t6,space0);
StructuredBuffer<Light>    Lights        : register(t7,space0);
StructuredBuffer<AliasEntry> AliasTable  : register(t8,space0);

Texture2D    Textures[]   : register(t0, space1);

SamplerState LinearWrap   : register(s0,space2);
SamplerState LinearClamp  : register(s1,space2);

// Globals accessor — keeps shader code clean
static Globals globals_ = GlobalsBuf[0];
#define viewProj     globals_.viewProj
#define view         globals_.view
#define proj         globals_.proj
#define invViewProj  globals_.invViewProj
#define cameraPos    globals_.cameraPos
#define screenSize   globals_.screenSize
#define nearPlane    globals_.nearPlane
#define farPlane     globals_.farPlane
#define lightCount   globals_.lightCount

float3 rotateByQuat(float3 v, float4 q)
{
	float3 u = q.xyz;
	float  s = q.w;
	return 2.0 * dot(u, v) * u
	     + (s * s - dot(u, u)) * v
	     + 2.0 * s * cross(u, v);
}

float3 octDecode(float2 e)
{
	e = e * 2.0 - 1.0;
	float3 v = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if (v.z < 0.0)
		v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
	return normalize(v);
}

float3 unpackNormal1010102(uint packed)
{
	int x = (int)(packed << 22) >> 22;
	int y = (int)(packed << 12) >> 22;
	int z = (int)(packed <<  2) >> 22;
	return normalize(float3(x, y, z) / 511.0);
}

float getBitangentSign(uint packed)
{
	return (packed >> 30) >= 2 ? -1.0 : 1.0;
}

float3 unpackTangentOct88(uint packed16)
{
	float u = float(packed16 & 0xFF) / 255.0;
	float v = float((packed16 >> 8) & 0xFF) / 255.0;
	return octDecode(float2(u, v));
}

float3 dequantizePosition(uint3 q, float3 center, float radius)
{
	return center + (float3(q) / 65535.0 * 2.0 - 1.0) * radius;
}

float2 dequantizeUV(uint2 q)
{
	return float2(f16tof32(q.x), f16tof32(q.y));
}

struct UnpackedVertex
{
	float3 position;
	float3 normal;
	float3 tangent;
	float  bitangentSign;
	float2 uv;
};

UnpackedVertex loadVertex(uint globalIndex, float3 meshCenter, float meshRadius)
{
	uint addr = globalIndex * 16;

	uint word0 = VertexBuffer.Load(addr);
	uint word1 = VertexBuffer.Load(addr + 4);
	uint word2 = VertexBuffer.Load(addr + 8);
	uint word3 = VertexBuffer.Load(addr + 12);

	UnpackedVertex o;
	o.position      = dequantizePosition(uint3(word0 & 0xFFFF, word0 >> 16, word1 & 0xFFFF), meshCenter, meshRadius);
	o.normal        = unpackNormal1010102(word2);
	o.tangent       = unpackTangentOct88(word1 >> 16);
	o.bitangentSign = getBitangentSign(word2);
	o.uv            = dequantizeUV(uint2(word3 & 0xFFFF, word3 >> 16));

	return o;
}

struct UnpackedMeshlet
{
	float3 center;
	float  radius;
	float3 coneAxis;
	float  coneCutoff;
	uint   dataOffset;
	uint   baseVertex;
	uint   vertexCount;
	uint   triangleCount;
};

UnpackedMeshlet loadMeshlet(uint meshletIndex)
{
    uint addr = meshletIndex * 32;

    uint word0 = MeshletBuffer.Load(addr);
    uint word1 = MeshletBuffer.Load(addr + 4);
    uint word2 = MeshletBuffer.Load(addr + 8);
    uint word3 = MeshletBuffer.Load(addr + 12);
    uint word4 = MeshletBuffer.Load(addr + 16);
    uint word5 = MeshletBuffer.Load(addr + 20);
    uint word6 = MeshletBuffer.Load(addr + 24);
    uint word7 = MeshletBuffer.Load(addr + 28);

    UnpackedMeshlet ml;
    ml.center = float3(asfloat(word0), asfloat(word1), asfloat(word2));
    ml.radius = asfloat(word3);

    int b0 = (int)((word4)       << 24) >> 24;
    int b1 = (int)((word4 >> 8)  << 24) >> 24;
    int b2 = (int)((word4 >> 16) << 24) >> 24;
    int b3 = (int)((word4 >> 24) << 24) >> 24;
    ml.coneAxis   = float3(b0, b1, b2) / 127.0;
    ml.coneCutoff = float(b3) / 127.0;

    ml.dataOffset    = word5;
    ml.baseVertex    = word6;
    ml.vertexCount   = (word7)      & 0xFF;
    ml.triangleCount = (word7 >> 8) & 0xFF;
    return ml;
}

#endif // COMMON_HLSLI
