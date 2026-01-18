#pragma once
#include "common.h"

struct Image{
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct Buffer{
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
};

VkCommandPool createCommandPool(VkDevice device, uint32_t familyIndex);
void createCommandBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBuffer &commandBuffer);

Buffer createBuffer(VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
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
