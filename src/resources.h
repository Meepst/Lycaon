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
Buffer createBuffer(VkDevice device, VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkDeviceSize alignment);
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

void stageBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 srcStageMask,VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask);

void buildBLAS(VkDevice device, VmaAllocator allocator,std::vector<Mesh>& meshes, const Buffer& vertBuffer, const Buffer& indexBuffer,
    std::vector<VkAccelerationStructureKHR>& blas, std::vector<VkDeviceSize>& compactedSizes, Buffer& blasBuffer,
    VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue,VkPhysicalDeviceAccelerationStructurePropertiesKHR deviceProperties);

void compactBLAS(VkDevice device, VmaAllocator allocator,std::vector<VkAccelerationStructureKHR>& blas,const std::vector<VkDeviceSize>& compactedSizes,
    Buffer& blasBuffer, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue);

VkAccelerationStructureKHR createTLAS(VkDevice device, VmaAllocator allocator,Buffer& stagingBuffer, const Buffer& instanceBuffer,
    uint32_t primitiveCount, Buffer& tlasBuffer,VkPhysicalDeviceAccelerationStructurePropertiesKHR deviceProperties);

void buildTlas(VkDevice device, VkCommandBuffer commandBuffer, VkAccelerationStructureKHR tlas, const Buffer& tlasBuffer,
    const Buffer& stagingBuffer, const Buffer& instanceBuffer, uint32_t primitiveCount,VkBuildAccelerationStructureModeKHR mode);

void fillInstanceRT(VkAccelerationStructureInstanceKHR& instance,const MeshDraw& draw, uint32_t instanceIndex,VkDeviceAddress blas);
