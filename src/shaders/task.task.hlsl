#include "common.hlsli"
#define TASK_GROUP_SIZE 32

struct TaskConstants
{
    uint drawIndex;
    uint meshletOffset;
    uint meshletCount;
    uint _pad;
};
[[vk::push_constant]] TaskConstants taskCB;

struct MeshPayload
{
    uint meshletIndices[TASK_GROUP_SIZE];
    uint drawIndex;
};

groupshared MeshPayload s_payload;

void extractFrustumPlanes(float4x4 vp, out float4 planes[6])
{
    // Gribb-Hartmann: extract from rows of the transposed VP matrix
    float4 r0 = float4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    float4 r1 = float4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    float4 r2 = float4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    float4 r3 = float4(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
    planes[0] = r3 + r0; // left
    planes[1] = r3 - r0; // right
    planes[2] = r3 + r1; // bottom
    planes[3] = r3 - r1; // top
    planes[4] = r2;       // near
    planes[5] = r3 - r2; // far
    [unroll] for (uint i = 0; i < 6; ++i)
        planes[i] /= length(planes[i].xyz);
}

bool frustumCullSphere(float3 center, float radius, float4 planes[6])
{
    [unroll] for (uint i = 0; i < 6; ++i)
    {
        if (dot(planes[i].xyz, center) + planes[i].w + radius < 0.0)
            return true;
    }
    return false;
}

bool coneCull(float3 meshletWorldCenter, float3 coneAxis, float coneCutoff, float3 camPos)
{
    float3 toCamera = normalize(camPos - meshletWorldCenter);
    return dot(toCamera, coneAxis) < coneCutoff;
}

[numthreads(TASK_GROUP_SIZE, 1, 1)]
void main(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
    uint meshletLocalIndex = gid * TASK_GROUP_SIZE + gtid;
    uint globalMeshletIndex = taskCB.meshletOffset + meshletLocalIndex;
    if (meshletLocalIndex < taskCB.meshletCount)
        s_payload.meshletIndices[gtid] = globalMeshletIndex;
    if (gtid == 0)
        s_payload.drawIndex = taskCB.drawIndex;
    GroupMemoryBarrierWithGroupSync();
    uint groupBase = gid * TASK_GROUP_SIZE;
    uint remaining = 0;
    if (taskCB.meshletCount > groupBase)
        remaining = taskCB.meshletCount - groupBase;
    uint survivors = min((uint)TASK_GROUP_SIZE, remaining);
    if (gtid == 0)
        printf("gid=%u offset=%u count=%u survivors=%u drawIndex=%u\n",
               gid, taskCB.meshletOffset, taskCB.meshletCount, survivors, taskCB.drawIndex);
    DispatchMesh(survivors, 1, 1, s_payload);
}
