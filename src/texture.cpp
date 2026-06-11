#include "texture.h"
#include "vulkan/vulkan_core.h"

#define BCDEC_IMPLEMENTATION
#include <bcdec.h>

#include <memory>

struct DDS_PIXELFORMAT{
    unsigned int dwSize;
	unsigned int dwFlags;
	unsigned int dwFourCC;
	unsigned int dwRGBBitCount;
	unsigned int dwRBitMask;
	unsigned int dwGBitMask;
	unsigned int dwBBitMask;
	unsigned int dwABitMask;
};

struct DDS_HEADER
{
	unsigned int dwSize;
	unsigned int dwFlags;
	unsigned int dwHeight;
	unsigned int dwWidth;
	unsigned int dwPitchOrLinearSize;
	unsigned int dwDepth;
	unsigned int dwMipMapCount;
	unsigned int dwReserved1[11];
	DDS_PIXELFORMAT ddspf;
	unsigned int dwCaps;
	unsigned int dwCaps2;
	unsigned int dwCaps3;
	unsigned int dwCaps4;
	unsigned int dwReserved2;
};

struct DDS_HEADER_DXT10
{
	unsigned int dxgiFormat;
	unsigned int resourceDimension;
	unsigned int miscFlag;
	unsigned int arraySize;
	unsigned int miscFlags2;
};

const unsigned int DDSCAPS2_CUBEMAP = 0x200;
const unsigned int DDSCAPS2_VOLUME = 0x200000;

const unsigned int DDS_DIMENSION_TEXTURE2D = 3;

enum DXGI_FORMAT
{
	DXGI_FORMAT_BC1_UNORM = 71,
	DXGI_FORMAT_BC1_UNORM_SRGB = 72,
	DXGI_FORMAT_BC2_UNORM = 74,
	DXGI_FORMAT_BC2_UNORM_SRGB = 75,
	DXGI_FORMAT_BC3_UNORM = 77,
	DXGI_FORMAT_BC3_UNORM_SRGB = 78,
	DXGI_FORMAT_BC4_UNORM = 80,
	DXGI_FORMAT_BC4_SNORM = 81,
	DXGI_FORMAT_BC5_UNORM = 83,
	DXGI_FORMAT_BC5_SNORM = 84,
	DXGI_FORMAT_BC6H_UF16 = 95,
	DXGI_FORMAT_BC6H_SF16 = 96,
	DXGI_FORMAT_BC7_UNORM = 98,
	DXGI_FORMAT_BC7_UNORM_SRGB = 99,
};

static unsigned int fourCC(const char (&str)[5])
{
	return (unsigned(str[0]) << 0) | (unsigned(str[1]) << 8) | (unsigned(str[2]) << 16) | (unsigned(str[3]) << 24);
}

static VkFormat getFormat(const DDS_HEADER& header, const DDS_HEADER_DXT10& header10)
{
	if (header.ddspf.dwFourCC == fourCC("DXT1"))
		return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
	if (header.ddspf.dwFourCC == fourCC("DXT3"))
		return VK_FORMAT_BC2_UNORM_BLOCK;
	if (header.ddspf.dwFourCC == fourCC("DXT5"))
		return VK_FORMAT_BC3_UNORM_BLOCK;
	if (header.ddspf.dwFourCC == fourCC("ATI1"))
		return VK_FORMAT_BC4_UNORM_BLOCK;
	if (header.ddspf.dwFourCC == fourCC("ATI2"))
		return VK_FORMAT_BC5_UNORM_BLOCK;

	if (header.ddspf.dwFourCC == fourCC("DX10"))
	{
		switch (header10.dxgiFormat)
		{
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
			return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
			return VK_FORMAT_BC2_UNORM_BLOCK;
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
			return VK_FORMAT_BC3_UNORM_BLOCK;
		case DXGI_FORMAT_BC4_UNORM:
			return VK_FORMAT_BC4_UNORM_BLOCK;
		case DXGI_FORMAT_BC4_SNORM:
			return VK_FORMAT_BC4_SNORM_BLOCK;
		case DXGI_FORMAT_BC5_UNORM:
			return VK_FORMAT_BC5_UNORM_BLOCK;
		case DXGI_FORMAT_BC5_SNORM:
			return VK_FORMAT_BC5_SNORM_BLOCK;
		case DXGI_FORMAT_BC6H_UF16:
			return VK_FORMAT_BC6H_UFLOAT_BLOCK;
		case DXGI_FORMAT_BC6H_SF16:
			return VK_FORMAT_BC6H_SFLOAT_BLOCK;
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return VK_FORMAT_BC7_UNORM_BLOCK;
		}
	}

	return VK_FORMAT_UNDEFINED;
}

static size_t getImageSizeBC(unsigned int width, unsigned int height, unsigned int levels, unsigned int blockSize)
{
	size_t result = 0;

	for (unsigned int i = 0; i < levels; ++i)
	{
		result += ((width + 3) / 4) * ((height + 3) / 4) * blockSize;

		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
	}

	return result;
}

bool createDDSImage(Image& image, VkDevice device, VmaAllocator allocator, VkCommandPool commandPool,
    VkCommandBuffer commandBuffer, VkQueue queue, std::vector<uint8_t> staging, const char* path){
    FILE* file = nullptr;
    errno_t err = fopen_s(&file, path, "rb");

    if(err != 0 || !file){
        printf("Failed to open file: %s\n Error Code: %d\n",path,err);
        return false;
    }

    std::unique_ptr<FILE,int (*)(FILE*)> filePtr(file,fclose);

    unsigned int magic = 0;
    if(fread(&magic,sizeof(magic),1,file) != 1 || magic != fourCC("DDS ")){
        return false;
    }

    DDS_HEADER header = {};
    if(fread(&header,sizeof(header),1,file)!=1){
        return false;
    }

    DDS_HEADER_DXT10 header10 = {};
    if(header.ddspf.dwFourCC == fourCC("DX10")&&fread_s(&header10,sizeof(header10),sizeof(size_t),1,file)!=1){
        return false;
    }

    if(header.dwSize != sizeof(header) || header.ddspf.dwSize != sizeof(header.ddspf)){
        return false;
    }

    if(header.dwCaps2 & (DDSCAPS2_CUBEMAP | DDSCAPS2_VOLUME)){
        return false;
    }

    if(header.ddspf.dwFourCC == fourCC("DX10") && header10.resourceDimension != DDS_DIMENSION_TEXTURE2D){
        return false;
    }

    VkFormat format = getFormat(header,header10);
    if(format == VK_FORMAT_UNDEFINED){
        return false;
    }

    uint32_t blockSize = (format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK || format == VK_FORMAT_BC4_SNORM_BLOCK || format == VK_FORMAT_BC4_UNORM_BLOCK) ? 8 : 16;
    size_t imageSize = getImageSizeBC(header.dwWidth,header.dwHeight,header.dwMipMapCount,blockSize);
    printf("Image size: %zu\n",imageSize);
    if(imageSize == 0){
        printf("No image size\n");
        return false;
    }

    size_t readSize = fread(staging.data(),1,imageSize,file);
    if(readSize != imageSize){
        printf("Read size does not match image size");
        return false;
    }

    // makes sure we read whole file, -1 is eof
    if(fgetc(file)!=-1){
        return false;
    }

    filePtr.reset();
    file = nullptr;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image = createImage(allocator,device,queue,commandBuffer,staging.data(),VkExtent3D(header.dwWidth,header.dwHeight,header.dwMipMapCount),
        format,usage,true);

    return true;
}



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
