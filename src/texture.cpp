#include "texture.h"

Image createKTXImage(VmaAllocator allocator,VkDevice device, VkQueue queue,
    VkCommandBuffer commandBuffer, ktxTexture2* texture, VkImageUsageFlags usage){
    if(ktxTexture2_NeedsTranscoding(texture)){
        ktx_transcode_fmt_e target = KTX_TTF_BC7_RGBA;
        KTX_error_code result = ktxTexture2_TranscodeBasis(texture, target, 0);
        assert(result == KTX_SUCCESS);
    }

    VkFormat format = (VkFormat)ktxTexture2_GetVkFormat(texture);
    uint32_t mipLevels = texture->numLevels;
    uint32_t width = texture->baseWidth;
    uint32_t height = texture->baseHeight;

    VkImageCreateInfo imageCreateInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.extent = {width,height,1};
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.mipLevels = mipLevels;
    imageCreateInfo.usage = usage;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;


    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    Image newImage{};
    VK_CHECK(vmaCreateImage(allocator,&imageCreateInfo,
        &allocInfo,&newImage.image,&newImage.allocation,nullptr));

    newImage.imageFormat = format;
    newImage.imageExtent = VkExtent3D{width,height,1};

    VkImageViewCreateInfo viewInfo = createImageViewInfo(format, newImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    viewInfo.subresourceRange.levelCount = imageCreateInfo.mipLevels;

    VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &newImage.imageView));


    ktx_size_t dataSize = ktxTexture_GetDataSize(ktxTexture(texture));
    const ktx_uint8_t* dataPtr = ktxTexture_GetData(ktxTexture(texture));


    Buffer stagingBuffer = createBuffer(device,allocator,dataSize,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VMA_MEMORY_USAGE_AUTO);
    void* mapped = nullptr;
    vmaMapMemory(allocator,stagingBuffer.allocation,&mapped);
    memcpy(mapped,dataPtr,dataSize);
    vmaUnmapMemory(allocator,stagingBuffer.allocation);

    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

    VkCommandBufferBeginInfo cmdBufferBeginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo));

    std::vector<VkBufferImageCopy2> regions(mipLevels);
    for(uint32_t i=0;i<mipLevels;i++){
        ktx_size_t offset = 0;
        ktxTexture_GetImageOffset(ktxTexture(texture), i, 0, 0, &offset);

        uint32_t mipw = std::max(1u,width>>i);
        uint32_t miph = std::max(1u,height>>i);

        regions[i] = {.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
        regions[i].bufferOffset = offset;
        regions[i].bufferRowLength = 0;
        regions[i].bufferImageHeight = 0;
        regions[i].imageSubresource = {
            VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1
        };
        regions[i].imageOffset = {0,0,0};
        regions[i].imageExtent = {mipw,miph,1};
    }

    {
        VkImageMemoryBarrier2 toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toDst.srcStageMask  = VK_PIPELINE_STAGE_2_NONE;
        toDst.srcAccessMask = 0;
        toDst.dstStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
        toDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toDst.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.image         = newImage.image;
        toDst.subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1
        };
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers    = &toDst;
        vkCmdPipelineBarrier2(commandBuffer, &dep);
    }

    VkCopyBufferToImageInfo2 copyInfo{VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2};
    copyInfo.srcBuffer      = stagingBuffer.buffer;
    copyInfo.dstImage       = newImage.image;
    copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copyInfo.regionCount    = (uint32_t)regions.size();
    copyInfo.pRegions       = regions.data();
    vkCmdCopyBufferToImage2(commandBuffer, &copyInfo);

    {
        VkImageMemoryBarrier2 toShader{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toShader.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
        toShader.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toShader.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                                | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        toShader.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toShader.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.image         = newImage.image;
        toShader.subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1
        };
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers    = &toShader;
        vkCmdPipelineBarrier2(commandBuffer, &dep);
    }

    VK_CHECK(vkEndCommandBuffer(commandBuffer));
    VkCommandBufferSubmitInfo subInfo = createCommandBufferSubmitInfo(commandBuffer);
    VkSubmitInfo2 submitInfo = createDrawSubmitInfo(&subInfo, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, nullptr));
    vkDeviceWaitIdle(device);

    destroyBuffer(allocator, stagingBuffer);
    return newImage;
}
