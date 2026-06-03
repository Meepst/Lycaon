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
    VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.oldLayout = currentLayout;
        barrier.newLayout = newLayout;
        barrier.image     = image;

        switch (currentLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                  | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_NONE;
            break;
        case VK_IMAGE_LAYOUT_GENERAL:
            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        default:
            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        }

        switch (newLayout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
                                  | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            barrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                  | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                  | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            barrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            barrier.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_NONE;
            break;
        case VK_IMAGE_LAYOUT_GENERAL:
            barrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
                                  | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        default:
            barrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
                                  | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        }

        // Determine aspect mask from the actual layouts
        bool isDepth = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                     || newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                     || currentLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                     || currentLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        barrier.subresourceRange = createImageSubresourceRange(
            isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);

        VkDependencyInfo depInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers    = &barrier;
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

    *outShaderModule = shaderModule;
    return true;
}

Buffer createBuffer(VkDevice device,VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage){
    VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo vmaAllocInfo{};
    vmaAllocInfo.usage = memoryUsage;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    vmaAllocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                            | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    Buffer newBuffer;
    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer,&newBuffer.allocation,&newBuffer.info));

    VkBufferDeviceAddressInfo addressInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    addressInfo.buffer = newBuffer.buffer;
    newBuffer.address = vkGetBufferDeviceAddress(device, &addressInfo);

    newBuffer.size = allocSize;

    return newBuffer;
}

Buffer createBuffer(VkDevice device, VmaAllocator allocator, size_t allocSize,
    VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkDeviceSize alignment){
    VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo vmaAllocInfo{};
    vmaAllocInfo.usage = memoryUsage;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    vmaAllocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    Buffer newBuffer;
    VK_CHECK(vmaCreateBufferWithAlignment(allocator, &bufferInfo, &vmaAllocInfo,alignment,
        &newBuffer.buffer,&newBuffer.allocation,&newBuffer.info));

    VkBufferDeviceAddressInfo addressInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    addressInfo.buffer = newBuffer.buffer;
    newBuffer.address = vkGetBufferDeviceAddress(device, &addressInfo);

    newBuffer.size = allocSize;

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
        newImage.mipLevels = imageInfo.mipLevels;
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
    Buffer uploadBuffer = createBuffer(device, allocator, dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
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

	uint32_t mipLevels = getImageMipLevels(size.width, size.height);
	mipLevels = 1;
    int32_t mipWidth = size.width;
    int32_t mipHeight = size.height;

    for (uint32_t i = 1; i < mipLevels; i++) {
        // Transition level i-1: TRANSFER_DST → TRANSFER_SRC
        VkImageMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.image = newImage.image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1 };

        VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &depInfo);

        // Blit from level i-1 to level i
        VkImageBlit2 blit = { VK_STRUCTURE_TYPE_IMAGE_BLIT_2 };
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { std::max(mipWidth / 2, 1), std::max(mipHeight / 2, 1), 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };

        VkBlitImageInfo2 blitInfo = { VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2 };
        blitInfo.srcImage = newImage.image;
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.dstImage = newImage.image;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blit;
        blitInfo.filter = VK_FILTER_LINEAR;

        vkCmdBlitImage2(commandBuffer, &blitInfo);

        mipWidth = std::max(mipWidth / 2, 1);
        mipHeight = std::max(mipHeight / 2, 1);
    }

    // Transition all levels to SHADER_READ_ONLY
    // Levels 0 through mipLevels-2 are in TRANSFER_SRC, last level is in TRANSFER_DST
    VkImageMemoryBarrier2 finalBarriers[2] = {};

    finalBarriers[0] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    finalBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    finalBarriers[0].srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    finalBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    finalBarriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    finalBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    finalBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    finalBarriers[0].image = newImage.image;
    finalBarriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels - 1, 0, 1 };

    finalBarriers[1] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    finalBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    finalBarriers[1].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    finalBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    finalBarriers[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    finalBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    finalBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    finalBarriers[1].image = newImage.image;
    finalBarriers[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, 1 };

    VkDependencyInfo finalDep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    finalDep.imageMemoryBarrierCount = (mipLevels > 1) ? 2 : 1;
    finalDep.pImageMemoryBarriers = (mipLevels > 1) ? finalBarriers : &finalBarriers[1];
    vkCmdPipelineBarrier2(commandBuffer, &finalDep);
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

    while(width > 1 || height > 1){
        result++;
        width/=2;
        height/=2;
    }

    return result;
}

void stageBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 srcStageMask,VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask){
    VkMemoryBarrier2 memoryBarrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    memoryBarrier.srcAccessMask = srcAccessMask;
    memoryBarrier.srcStageMask = srcStageMask;
    memoryBarrier.dstAccessMask = dstAccessMask;
    memoryBarrier.dstStageMask = dstStageMask;

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.memoryBarrierCount = 1;
    depInfo.pMemoryBarriers = &memoryBarrier;

    vkCmdPipelineBarrier2(commandBuffer,&depInfo);
}

void buildBLAS(VkDevice device, VmaAllocator allocator,std::vector<Mesh>& meshes, const Buffer& vertBuffer, const Buffer& indexBuffer,
    std::vector<VkAccelerationStructureKHR>& blas, std::vector<VkDeviceSize>& compactedSizes, Buffer& blasBuffer,
    VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue,
    VkPhysicalDeviceAccelerationStructurePropertiesKHR deviceProperties){
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
    std::vector<size_t> stageSizes(meshes.size());

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
        stageSizes[i] = sizeInfo.buildScratchSize;

        totalAccelerationSize = (totalAccelerationSize+sizeInfo.accelerationStructureSize+kAlignment-1)&~(kAlignment-1);
        totalPrimitiveCount += primitiveCounts[i];
        maxScratchSize = std::max(maxScratchSize,size_t(sizeInfo.buildScratchSize));
    }

    blasBuffer = createBuffer(device, allocator, totalAccelerationSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    Buffer stagingBuffer = createBuffer(device,allocator,std::max(kDefaultScratch, maxScratchSize),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VMA_MEMORY_USAGE_AUTO,deviceProperties.minAccelerationStructureScratchOffsetAlignment);

    printf("BLAS accelerationStructureSize: %.2f MB, scratchSize: %.2f MB (max %.2f MB), %.3fM triangles\n", double(totalAccelerationSize) / 1e6, double(stagingBuffer.size) / 1e6, double(maxScratchSize) / 1e6, double(totalPrimitiveCount) / 1e6);


    VkDeviceAddress stageAddress = stagingBuffer.address;

    blas.resize(meshes.size());
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges(meshes.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> buildRangePtrs(meshes.size());

    for(size_t i=0;i<meshes.size();i++){
        VkAccelerationStructureCreateInfoKHR accelInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        accelInfo.buffer = blasBuffer.buffer;
        accelInfo.offset = accelerationOffsets[i];
        accelInfo.size = accelerationSizes[i];
        accelInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        VK_CHECK(vkCreateAccelerationStructureKHR(device, &accelInfo, nullptr, &blas[i]));
    }

    VkQueryPoolCreateInfo qpInfo{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qpInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
    qpInfo.queryCount = uint32_t(meshes.size());

    VkQueryPool queryPool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateQueryPool(device,&qpInfo,nullptr,&queryPool));

    VK_CHECK(vkResetCommandPool(device,commandPool,0));

    VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    vkCmdResetQueryPool(commandBuffer,queryPool,0,uint32_t(meshes.size()));

    for(size_t start = 0;start<meshes.size();){
        size_t stageOffset = 0;

        size_t i = start;
        while(i<meshes.size()&&stageOffset+stageSizes[i]<=stagingBuffer.size){
            buildInfos[i].scratchData.deviceAddress = stageAddress+stageOffset;
            buildInfos[i].dstAccelerationStructure = blas[i];
            buildRanges[i].primitiveCount = primitiveCounts[i];
            buildRangePtrs[i] = &buildRanges[i];

            stageOffset = (stageOffset+stageSizes[i]+kAlignment-1)&~(kAlignment-1);
            i++;
        }
        assert(i>start);

        vkCmdBuildAccelerationStructuresKHR(commandBuffer, uint32_t(i - start), &buildInfos[start], &buildRangePtrs[start]);
        start = i;


        VkAccessFlags2 accessFlags = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        stageBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,accessFlags,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,accessFlags);
    }

    vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer,uint32_t(blas.size()),blas.data(),VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
        queryPool,0);

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VK_CHECK(vkQueueSubmit(queue,1,&submitInfo,VK_NULL_HANDLE));
    VK_CHECK(vkDeviceWaitIdle(device));

    compactedSizes.resize(meshes.size());
    VK_CHECK(vkGetQueryPoolResults(device,queryPool,0,uint32_t(compactedSizes.size()),
        compactedSizes.size()*sizeof(VkDeviceSize), compactedSizes.data(),sizeof(VkDeviceSize),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

    vkDestroyQueryPool(device,queryPool,nullptr);
    destroyBuffer(allocator,stagingBuffer);
}

void compactBLAS(VkDevice device, VmaAllocator allocator,std::vector<VkAccelerationStructureKHR>& blas,const std::vector<VkDeviceSize>& compactedSizes,
    Buffer& blasBuffer, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue){
    const size_t kAlignment = 256;

    VK_CHECK(vkResetCommandPool(device,commandPool,0));

    size_t totalCompactedSize = 0;
    std::vector<size_t> compactedOffsets(blas.size());

    for(size_t i=0;i<blas.size();i++){
        compactedOffsets[i] = totalCompactedSize;
        totalCompactedSize = (totalCompactedSize + compactedSizes[i] + kAlignment - 1) & ~(kAlignment - 1);
    }

    printf("BLAS compacted accelerationStructureSize: %.2f MB\n",double(totalCompactedSize)/1e6);

    Buffer compactedBuffer = createBuffer(device,allocator,totalCompactedSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,VMA_MEMORY_USAGE_GPU_ONLY);

    std::vector<VkAccelerationStructureKHR> compactedBlas(blas.size());
    for(size_t i=0;i<blas.size();i++){
        VkAccelerationStructureCreateInfoKHR accelInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        accelInfo.buffer = compactedBuffer.buffer;
        accelInfo.offset = compactedOffsets[i];
        accelInfo.size = compactedSizes[i];
        accelInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        VK_CHECK(vkCreateAccelerationStructureKHR(device, &accelInfo, nullptr, &compactedBlas[i]));
    }

    VK_CHECK(vkResetCommandPool(device,commandPool,0));

    VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer,&beginInfo));

    for(size_t i=0;i<blas.size();i++){
        VkCopyAccelerationStructureInfoKHR copyInfo{.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
        copyInfo.src = blas[i];
        copyInfo.dst = compactedBlas[i];
        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;

        vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyInfo);
    }

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VK_CHECK(vkQueueSubmit(queue,1,&submitInfo,VK_NULL_HANDLE));
    VK_CHECK(vkDeviceWaitIdle(device));

    for(size_t i=0;i<blas.size();i++){
        vkDestroyAccelerationStructureKHR(device,blas[i],nullptr);
        blas[i] = compactedBlas[i];
    }

    destroyBuffer(allocator,blasBuffer);
    blasBuffer = compactedBuffer;
}

VkAccelerationStructureKHR createTLAS(VkDevice device, VmaAllocator allocator,Buffer& stagingBuffer, const Buffer& instanceBuffer,
    uint32_t primitiveCount, Buffer& tlasBuffer, VkPhysicalDeviceAccelerationStructurePropertiesKHR deviceProperties){
    VkAccelerationStructureGeometryKHR geometry = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = instanceBuffer.address;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags =  VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&buildInfo,&primitiveCount,&sizeInfo);

    printf("TLAS accelerationStructureSize: %.2f MB, scratchSize: %.2f MB, updateScratch: %.2f MB\n",
        double(sizeInfo.accelerationStructureSize) / 1e6, double(sizeInfo.buildScratchSize) / 1e6, double(sizeInfo.updateScratchSize) / 1e6);

    tlasBuffer = createBuffer(device,allocator,sizeInfo.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    stagingBuffer = createBuffer(device,allocator,std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, deviceProperties.minAccelerationStructureScratchOffsetAlignment);

    VkAccelerationStructureCreateInfoKHR accelerationInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    accelerationInfo.buffer = tlasBuffer.buffer;
    accelerationInfo.size = sizeInfo.accelerationStructureSize;
    accelerationInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    VkAccelerationStructureKHR tlas = nullptr;
    VK_CHECK(vkCreateAccelerationStructureKHR(device,&accelerationInfo,nullptr,&tlas));

    return tlas;
}

void buildTlas(VkDevice device, VkCommandBuffer commandBuffer, VkAccelerationStructureKHR tlas, const Buffer& tlasBuffer,
    const Buffer& stagingBuffer, const Buffer& instanceBuffer, uint32_t primitiveCount,VkBuildAccelerationStructureModeKHR mode){
    VkAccelerationStructureGeometryKHR geometry = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = instanceBuffer.address;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags =  VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.srcAccelerationStructure = tlas;
    buildInfo.dstAccelerationStructure = tlas;
    buildInfo.scratchData.deviceAddress = stagingBuffer.address;

    VkAccelerationStructureBuildRangeInfoKHR buildRange = {};
    buildRange.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* buildRangePtr = &buildRange;

    vkCmdBuildAccelerationStructuresKHR(commandBuffer,1,&buildInfo,&buildRangePtr);

    stageBarrier(commandBuffer,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}

void fillInstanceRT(VkAccelerationStructureInstanceKHR& instance,const MeshDraw& draw, uint32_t instanceIndex,VkDeviceAddress blas){
    glm::mat3 xform = glm::transpose(glm::mat3_cast(draw.orientation))*draw.scale;

    memcpy(instance.transform.matrix[0], &xform[0], sizeof(float) * 3);
	memcpy(instance.transform.matrix[1], &xform[1], sizeof(float) * 3);
	memcpy(instance.transform.matrix[2], &xform[2], sizeof(float) * 3);

	instance.transform.matrix[0][3] = draw.position.x;
	instance.transform.matrix[1][3] = draw.position.y;
	instance.transform.matrix[2][3] = draw.position.z;

	instance.instanceCustomIndex = instanceIndex;
	instance.mask = 1 << draw.postPass;
	instance.flags = draw.postPass ? 0 : VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	instance.accelerationStructureReference = draw.postPass <= 1 ? blas : 0;
}
