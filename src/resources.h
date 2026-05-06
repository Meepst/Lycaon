#pragma once
#include "common.h"
#include "scene.h"
#include "vulkan/vulkan_core.h"

struct Image{
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
    uint32_t mipLevels = 1;
};

struct Buffer{
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
    VkDeviceAddress address;
    VkDeviceSize size;
};

VkCommandPool createCommandPool(VkDevice device, uint32_t familyIndex);
void createCommandBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBuffer &commandBuffer);

Buffer createBuffer(VkDevice device, VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
void destroyBuffer(VmaAllocator allocator, const Buffer& buffer);

Image createImage(VmaAllocator allocator, VkDevice device, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipMapped);
Image createImage(VmaAllocator allocator, VkDevice device, VkQueue queue, VkCommandBuffer commandBuffer, void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipMapped);
void destroyImage(VkDevice device, VmaAllocator allocator, const Image& img);

VkSemaphoreSubmitInfo createSemaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);
VkImageSubresourceRange createImageSubresourceRange(VkImageAspectFlags aspectMask);
VkCommandBufferSubmitInfo createCommandBufferSubmitInfo(VkCommandBuffer commandBuffer);
VkSubmitInfo2 createDrawSubmitInfo(VkCommandBufferSubmitInfo* commandBufferSubmitInfo,
    VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo);
VkImageCreateInfo createImageCreateInfo(VkFormat format, VkImageUsageFlags flags, VkExtent3D extent);
VkImageViewCreateInfo createImageViewInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);


VkFence createFence(VkDevice device);
VkSemaphore createSemaphore(VkDevice device);

void transitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void copyImageToImage(VkCommandBuffer commandBuffer, VkImage src, VkImage dst, VkExtent2D srcSize, VkExtent2D dstSize);
bool loadShaderModule(const char* filename, VkDevice device, VkShaderModule* outShaderModule);
uint32_t getImageMipLevels(uint32_t width, uint32_t height);

void buildBLAS(VkDevice device, VmaAllocator allocator,std::vector<Mesh>& meshes, const Buffer& vertBuffer, const Buffer& indexBuffer,
    std::vector<VkAccelerationStructureKHR>& blas, std::vector<VkDeviceSize>& compactedSizes, Buffer& blasBuffer,
    VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const VkPhysicalDeviceMemoryProperties& memoryProperties){
    std::vector<uint32_t> primitiveCounts(meshes.size());
    std::vector<VkAccelerationStructureGeometryKHR> geometries(meshes.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos(meshes.size());

    const size_t kAlignment = 256;
    const size_t kDefaultScratch = 32*1024*1024;

    size_t totalAccelerationSize = 0;
    size_t totalPrimitiveCount = 0;
    size_t maxScratchSize = 0;

    std::vector<size_t> accelerationOffsets(meshes.size());
    std::vector<size_t> accelerationSizes(meshes.size());
    std::vector<size_t> scratchSizes(meshes.size());

    VkDeviceAddress vertAddress = vertBuffer.address;
    VkDeviceAddress indexAddress = indexBuffer.address;

    for(size_t i=0;i<meshes.size();i++){
        const Mesh& mesh = meshes[i];
        VkAccelerationStructureGeometryKHR& geo = geometries[i];
        VkAccelerationStructureBuildGeometryInfoKHR& buildInfo = buildInfos[i];

        unsigned int lodIndex = 0;

        primitiveCounts[i] = mesh.lods[lodIndex].indexCount/3;

        geo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;

        static_assert(offsetof(Vertex, vz) == offsetof(Vertex, vx) + sizeof(uint16_t) * 2, "Vertex layout mismatch");

        geo.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geo.geometry.triangles.vertexFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        geo.geometry.triangles.vertexData.deviceAddress = vertAddress+mesh.vertexOffset*sizeof(Vertex);
        geo.geometry.triangles.vertexStride = sizeof(Vertex);
        geo.geometry.triangles.maxVertex = mesh.vertexCount-1;
        geo.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geo.geometry.triangles.indexData.deviceAddress = indexAddress+mesh.lods[lodIndex].indexOffset*sizeof(uint32_t);

        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR|VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geo;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&buildInfo,&primitiveCounts[i],&sizeInfo);

        accelerationOffsets[i] = totalAccelerationSize;
        accelerationSizes[i] = sizeInfo.accelerationStructureSize;
        scratchSizes[i] = sizeInfo.buildScratchSize;

        totalAccelerationSize = (totalAccelerationSize+sizeInfo.accelerationStructureSize+kAlignment-1)&~(kAlignment-1);
        totalPrimitiveCount += primitiveCounts[i];
        maxScratchSize = std::max(maxScratchSize,size_t(sizeInfo.buildScratchSize));
    }

    blasBuffer = createBuffer(device, allocator, totalAccelerationSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    Buffer stagingBuffer = createBuffer(device,allocator,std::max(kDefaultScratch, maxScratchSize),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VMA_MEMORY_USAGE_AUTO);

    printf("BLAS accelerationStructureSize: %.2f MB, scratchSize: %.2f MB (max %.2f MB), %.3fM triangles\n", double(totalAccelerationSize) / 1e6, double(scratchBuffer.size) / 1e6, double(maxScratchSize) / 1e6, double(totalPrimitiveCount) / 1e6);

    VkDeviceAddress stageAddress = stagingBuffer.address;

    blas.resize(meshes.size());
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges(meshes.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> buildRangePtrs(meshes.size());

    for(size_t i=0;i<meshes.size();i++){

    }
}
