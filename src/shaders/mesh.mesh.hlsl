#include "common.hlsli"

#define MAX_VERTICES  64
#define MAX_TRIANGLES 126
#define THREADS       128

// Payload from task shader
struct MeshPayload
{
	uint meshletIndices[32];
	uint drawIndex;
};


[numthreads(THREADS, 1, 1)]
[outputtopology("triangle")]
void main(
    uint gtid : SV_GroupThreadID,
    uint gid  : SV_GroupID,
    in payload MeshPayload meshPayload,
    out vertices VertexOutput    outVerts[MAX_VERTICES],
    out indices  uint3           outTris[MAX_TRIANGLES])
{
    uint meshletIndex = meshPayload.meshletIndices[gid];
    uint drawIndex    = meshPayload.drawIndex;
    MeshDraw draw = Draws[drawIndex];
    Mesh mesh = Meshes[draw.meshIndex];
    UnpackedMeshlet ml   = loadMeshlet(meshletIndex);
    SetMeshOutputCounts(ml.vertexCount, ml.triangleCount);

    if (gtid < ml.vertexCount)
    {
        uint localIndex  = MeshletData[ml.dataOffset + gtid];
        uint globalIndex = ml.baseVertex + localIndex;
        UnpackedVertex v = loadVertex(globalIndex);
        float3 worldPos     = draw.position + rotateByQuat(v.position * draw.scale, draw.orientation);
        float3 worldNormal  = normalize(rotateByQuat(v.normal/draw.scale,  draw.orientation));
        float3 worldTangent = normalize(rotateByQuat(v.tangent*draw.scale, draw.orientation));
        VertexOutput o;
        o.clipPos       = mul(viewProj, float4(worldPos, 1.0));
        o.worldPos      = worldPos;
        o.worldNormal   = normalize(worldNormal);
        o.worldTangent  = normalize(worldTangent);
        o.bitangentSign = v.bitangentSign;
        o.uv            = v.uv;
        o.materialIndex = draw.materialIndex;
        outVerts[gtid] = o;
    }
    if (gtid < ml.triangleCount)
    {
        uint triDataStart = ml.dataOffset + ml.vertexCount;
        uint byteIndex    = gtid * 3;
        uint wordIndex    = byteIndex / 4;
        uint byteInWord   = byteIndex % 4;
        uint w0 = MeshletData[triDataStart + wordIndex];
        uint w1 = (byteInWord == 0) ? 0 : MeshletData[triDataStart + wordIndex + 1];
        // Shift so the 3 target bytes start at bit 0
        uint combined;
        if (byteInWord == 0)
            combined = w0;
        else
            combined = (w0 >> (byteInWord * 8)) | (w1 << ((4 - byteInWord) * 8));
        outTris[gtid] = uint3(
            (combined)       & 0xFF,
            (combined >> 8)  & 0xFF,
            (combined >> 16) & 0xFF
        );
    }
}
