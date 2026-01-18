#include "resources.h"
#include "common.h"
#include <vulkan/vulkan_core.h>

VkCommandPool createCommandPool(VkDevice device, uint32_t familyIndex){
    VkCommandPoolCreateInfo createInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    createInfo.queueFamilyIndex = familyIndex;

    VkCommandPool commandPool = 0;
    VK_CHECK(vkCreateCommandPool(device, &createInfo, nullptr, &commandPool));

    return commandPool;
}

VkImageSubresourceRange createImageSubresourceRange(VkImageAspectFlags aspectMask){
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = aspectMask;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    return subresourceRange;
}

void createCommandBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBuffer &commandBuffer){
    VkCommandBufferAllocateInfo allocInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));
}

VkFence createFence(VkDevice device){
    VkFenceCreateInfo createInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkFence fence = 0;
    VK_CHECK(vkCreateFence(device, &createInfo, nullptr, &fence));

    return fence;
}

VkSemaphore createSemaphore(VkDevice device){
    VkSemaphoreCreateInfo createInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkSemaphore semaphore = 0;
    VK_CHECK(vkCreateSemaphore(device, &createInfo, nullptr, &semaphore));

    return semaphore;
}

void transitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout){
    VkImageMemoryBarrier2 imageBarrier {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    imageBarrier.oldLayout = currentLayout;
    imageBarrier.newLayout = newLayout;

    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    imageBarrier.subresourceRange = createImageSubresourceRange(aspectMask);
    imageBarrier.image = image;

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &depInfo);
}

VkSemaphoreSubmitInfo createSemaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore){
    VkSemaphoreSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    submitInfo.semaphore = semaphore;
    submitInfo.stageMask = stageMask;
    submitInfo.deviceIndex = 0;
    submitInfo.value = 1;

    return submitInfo;
}

VkCommandBufferSubmitInfo createCommandBufferSubmitInfo(VkCommandBuffer commandBuffer){
    VkCommandBufferSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    submitInfo.commandBuffer = commandBuffer;
    submitInfo.deviceMask = 0;

    return submitInfo;
}

VkSubmitInfo2 createDrawSubmitInfo(VkCommandBufferSubmitInfo* commandBufferSubmitInfo,
    VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo){
    VkSubmitInfo2 submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = waitSemaphoreInfo == nullptr ? 0 : 1;
    submitInfo.pWaitSemaphoreInfos = waitSemaphoreInfo;
    submitInfo.signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0 : 1;
    submitInfo.pSignalSemaphoreInfos = signalSemaphoreInfo;

    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = commandBufferSubmitInfo;

    return submitInfo;
}

VkImageCreateInfo createImageCreateInfo(VkFormat format, VkImageUsageFlags flags, VkExtent3D extent){
    VkImageCreateInfo info{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = VkExtent3D(extent.width,extent.height,1);
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = flags;

    return info;
}

VkImageViewCreateInfo createImageViewInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags){
    VkImageViewCreateInfo info{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.image = image;
    info.format = format;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    info.subresourceRange.aspectMask = aspectFlags;

    return info;
}

void copyImageToImage(VkCommandBuffer commandBuffer, VkImage src, VkImage dst, VkExtent2D srcSize, VkExtent2D dstSize){
    VkImageBlit2 blitRegion{.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
    blitRegion.srcOffsets[1].x = srcSize.width;
	blitRegion.srcOffsets[1].y = srcSize.height;
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = dstSize.width;
	blitRegion.dstOffsets[1].y = dstSize.height;
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
	blitInfo.dstImage = dst;
	blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	blitInfo.srcImage = src;
	blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	blitInfo.filter = VK_FILTER_LINEAR;
	blitInfo.regionCount = 1;
	blitInfo.pRegions = &blitRegion;

	vkCmdBlitImage2(commandBuffer, &blitInfo);
}

bool loadShaderModule(const char* filename, VkDevice device, VkShaderModule* outShaderModule){
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if(!file.is_open()){
        return false;
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<uint32_t> buffer(fileSize/sizeof(uint32_t));
    file.seekg(0);
    file.read((char*)buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = buffer.size()*sizeof(uint32_t);
    createInfo.pCode = buffer.data();

    VkShaderModule shaderModule;
    if(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule)){
        return false;
    }
    printf("hi!\n");
    *outShaderModule = shaderModule;
    return true;
}

Buffer createBuffer(VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage){
    VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaAllocInfo{};
    vmaAllocInfo.usage = memoryUsage;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    Buffer newBuffer;
    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer,&newBuffer.allocation,&newBuffer.info));

    return newBuffer;
}

void destroyBuffer(VmaAllocator allocator, const Buffer& buffer){
    vmaDestroyBuffer(allocator,buffer.buffer,buffer.allocation);
}

Image createImage(VmaAllocator allocator, VkDevice device, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipMapped){
    Image newImage;
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo imageInfo = createImageCreateInfo(format, usage, size);
    if(mipMapped){
        imageInfo.mipLevels = getImageMipLevels(size.width, size.height);
    }

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(allocator, &imageInfo, &allocInfo, &newImage.image,&newImage.allocation,nullptr));

    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if(format == VK_FORMAT_D32_SFLOAT){
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkImageViewCreateInfo viewInfo = createImageViewInfo(format, newImage.image, aspectFlag);
    viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;

    VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &newImage.imageView));
    return newImage;
}

Image createImage(VmaAllocator allocator, VkDevice device, VkQueue queue, VkCommandBuffer commandBuffer,
    void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipMapped){
    size_t dataSize = size.depth*size.width*size.height;
    Buffer uploadBuffer = createBuffer(allocator, dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

    memcpy(uploadBuffer.info.pMappedData, data, dataSize);

    Image newImage = createImage(allocator, device, size, format, usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mipMapped);

    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

    VkCommandBufferBeginInfo cmdBufferBeginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo));

    transitionImage(commandBuffer, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.imageSubresource.mipLevel = 0;
	copyRegion.imageSubresource.baseArrayLayer = 0;
	copyRegion.imageSubresource.layerCount = 1;
	copyRegion.imageExtent = VkExtent3D(size.width,size.height,1);

	vkCmdCopyBufferToImage(commandBuffer, uploadBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

	transitionImage(commandBuffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkCommandBufferSubmitInfo subInfo = createCommandBufferSubmitInfo(commandBuffer);
    VkSubmitInfo2 submitInfo = createDrawSubmitInfo(&subInfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, nullptr));
    vkDeviceWaitIdle(device);

    destroyBuffer(allocator, uploadBuffer);
    return newImage;
}

void destroyImage(VkDevice device, VmaAllocator allocator, const Image& img){
    vkDestroyImageView(device, img.imageView,nullptr);
    vmaDestroyImage(allocator, img.image, img.allocation);
}

uint32_t getImageMipLevels(uint32_t width, uint32_t height){
    uint32_t result = 1;
    printf("height %d & width %d\n", height, width);
    while(width > 1 || height > 1){
        result++;
        width/=2;
        height/=2;
    }

    return result;
}
