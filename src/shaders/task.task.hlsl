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


[numthreads(TASK_GROUP_SIZE, 1, 1)]
void main(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
    uint meshletLocalIndex = gid * TASK_GROUP_SIZE + gtid;
    uint globalMeshletIndex = taskCB.meshletOffset + meshletLocalIndex;
    s_payload.meshletIndices[gtid] = globalMeshletIndex;  // unconditional
    s_payload.drawIndex = taskCB.drawIndex;
    GroupMemoryBarrierWithGroupSync();
    uint groupBase = gid * TASK_GROUP_SIZE;
    uint remaining = 0;
    if (taskCB.meshletCount > groupBase)
        remaining = taskCB.meshletCount - groupBase;
    uint survivors = min((uint)TASK_GROUP_SIZE, remaining);

    DispatchMesh(survivors, 1, 1, s_payload);
}
