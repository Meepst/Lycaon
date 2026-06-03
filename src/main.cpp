#define VMA_IMPLEMENTATION
#define VOLK_IMPLEMENTATION
#include "common.h"
#include "resources.h"
#include "scene.h"
#include "swapchain.h"
#include "volk.h"
#include <deque>
#include <functional>
#include <iostream>
#include <execution>
#include <stb_image.h>
#include "spirv_reflect.h"
#include "texture.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/type_ptr.hpp>F


struct FrameDescriptors {
  VkDevice device;
  void *descriptorHeap;
  size_t descriptorSize;
  uint32_t descriptorOffset;
  uint32_t descriptorOffsetEnd;
};

struct alignas(16) TaskConstants
{
	uint32_t drawIndex;
	uint32_t meshletOffset;
	uint32_t meshletCount;
	uint32_t _pad;
};

// struct PushConstants{
//     uint32_t globals;
//     uint32_t vertexBuffer;
//     uint32_t meshletData;
//     uint32_t meshletBuffer;
//     uint32_t meshes;
//     uint32_t draws;
//     uint32_t materials;
// };

struct alignas(64) Globals{
    glm::mat4 viewProj;
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 invViewProj;
    glm::vec3 cameraPos;
    float     _pad0;
    float     _pad1;
    uint32_t lightCount;
    glm::vec2 screenSize;
    float     nearPlane;
    float     farPlane;
};

struct Program{
    VkPipeline pipeline = VK_NULL_HANDLE;
    uint32_t descriptorCount = 0;
    uint32_t resourceMask = 0;
    uint32_t samplerMask = 0;
    VkDescriptorType resourceTypes[32] = {};
    std::string resourceNames[32] = {};
    std::string samplerNames[32] = {};
    VkDeviceSize descriptorSize = 0;
    uint32_t pushConstantSize = 0;
    uint32_t pushDescriptorCount = 0;
};

struct DecodedImage{
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    bool ok = false;
    std::string label;
};

// Globals
VkPhysicalDevice m_physicalDevice;
VkDevice m_device;
VmaAllocator m_vmaAllocator;

void drawBackground(VkCommandBuffer commandBuffer, VkImage image) {
  VkClearColorValue clearValue{};
  clearValue = {{0.2f, 0.3f, 0.4f, 1.0f}};

  VkImageSubresourceRange clearRange =
      createImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

  vkCmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL,
                       &clearValue, 1, &clearRange);
}

Buffer uploadBuffer(VkDevice device, VkQueue queue,
                    VkCommandBuffer commandBuffer, VmaAllocator allocator,
                    VkBufferUsageFlags usage, void *data, VkDeviceSize size) {

  Buffer newBuffer =
      createBuffer(m_device,allocator, size, usage, VMA_MEMORY_USAGE_GPU_ONLY);

  Buffer stagingBuffer =
      createBuffer(m_device,allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VMA_MEMORY_USAGE_CPU_ONLY);
  void *scratch = stagingBuffer.allocation->GetMappedData();

  memcpy(scratch, data, size);

  VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

  VkCommandBufferBeginInfo cmdBufferBeginInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo));

  VkBufferCopy bufferCopy{0};
  bufferCopy.dstOffset = 0;
  bufferCopy.srcOffset = 0;
  bufferCopy.size = size;

  vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, newBuffer.buffer, 1,
                  &bufferCopy);
  VK_CHECK(vkEndCommandBuffer(commandBuffer));

  VkCommandBufferSubmitInfo subInfo =
      createCommandBufferSubmitInfo(commandBuffer);
  VkSubmitInfo2 submitInfo = createDrawSubmitInfo(&subInfo, nullptr, nullptr);

  VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, nullptr));
  vkDeviceWaitIdle(device); // should change to proper synchronization

  destroyBuffer(allocator, stagingBuffer);
  return newBuffer;
}

static VkSpirvResourceTypeFlagsEXT getResourceTypeMask(VkDescriptorType type)
{
	switch (type)
	{
	case VK_DESCRIPTOR_TYPE_SAMPLER:
		return VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		return VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT;
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		return VK_SPIRV_RESOURCE_TYPE_READ_ONLY_IMAGE_BIT_EXT | VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT;
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		return VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT;
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		return VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT | VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT;
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		return VK_SPIRV_RESOURCE_TYPE_COMBINED_SAMPLED_IMAGE_BIT_EXT;
	default:
		assert(!"Unhandled descriptor type");
		return VK_SPIRV_RESOURCE_TYPE_ALL_EXT;
	}
}

void gatherResources(std::vector<SpvReflectShaderModule*> modules, uint32_t& resourceMask, uint32_t targetSet
    ,VkDescriptorType (&resourceTypes)[32], std::string (&resourceNames)[32])
{
    resourceMask = 0;

    for (SpvReflectShaderModule* mod : modules){
        // Get descriptor bindings for this module
        uint32_t bindingCount = 0;
        spvReflectEnumerateDescriptorBindings(mod, &bindingCount, nullptr);
        std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
        spvReflectEnumerateDescriptorBindings(mod, &bindingCount, bindings.data());

        for (uint32_t i = 0; i < bindingCount; ++i){
            const SpvReflectDescriptorBinding* desc = bindings[i];

            if (desc->set != targetSet)
                continue;

            // Skip bindings that are declared but never actually used by the entry point.
            // This is the main source of "superfluous" collisions — a binding may appear
            // in one stage as unused and conflict with its real type in another stage.
            if (!desc->accessed)
                continue;

            VkDescriptorType type = static_cast<VkDescriptorType>(desc->descriptor_type);
            uint32_t binding = desc->binding;
            assert(binding < 32);

            const char* name = desc->name ? desc->name : "";

            const bool alreadySeen = (resourceMask & (1u << binding)) != 0;
            const bool incomingIsSampler = (type == VK_DESCRIPTOR_TYPE_SAMPLER);
            const bool existingIsSampler = alreadySeen &&
                (resourceTypes[binding] == VK_DESCRIPTOR_TYPE_SAMPLER);

            if (alreadySeen){
                // Types must match, OR one side is a sampler aliasing a
                // sampled image at the same binding (combined-image-sampler style).
                const bool typesMatch = (resourceTypes[binding] == type);
                const bool samplerAliasing = incomingIsSampler || existingIsSampler;
                assert(typesMatch || samplerAliasing);
            }

            // Non-sampler always wins. Sampler only fills an empty slot.
            if (!alreadySeen || (!incomingIsSampler && existingIsSampler)){
                resourceTypes[binding] = type;
            }

            resourceMask |= (1u << binding);

            // Record the name from the non-sampler entry (usually the texture name).
            // Fall back to the sampler's name if nothing else has claimed the slot.
            if (name[0] != '\0'){
                const bool slotHasNoName = resourceNames[binding].empty();
                if (!incomingIsSampler || slotHasNoName)
                        resourceNames[binding] = name;
            }
        }
    }
}

static VkShaderDescriptorSetAndBindingMappingInfoEXT generateHeapMapping(
    uint32_t resourceMask, const VkDescriptorType (&resourceTypes)[32], const std::string (&resourceNames)[32],
    uint32_t samplerMask, const std::string (&samplerBindingNames)[32],
    size_t pushConstantSize, size_t descriptorSize,
    VkDescriptorSetAndBindingMappingEXT (&mappings)[34])
{
	uint32_t mappingOffset = 0;
	uint32_t descriptorOffset = 0;

	// samplerhack: name->id correspondence for embedded sampler offsets
	static const char* knownSamplerNames[] = {
		"textureSampler",
		"filterSampler",
		"depthSampler",
		"LinearWrap"
	};

	// push descriptors (non-sampler resources at set 0)
	for (uint32_t i = 0; i < 32; ++i)
	{
		if (!(resourceMask & (1 << i)))
			continue;
		if (resourceTypes[i] == VK_DESCRIPTOR_TYPE_SAMPLER)
			continue;

		VkDescriptorSetAndBindingMappingEXT& mapping = mappings[mappingOffset++];
		mapping.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT;
		mapping.descriptorSet = 0;
		mapping.firstBinding = i;
		mapping.bindingCount = 1;

		// use specific resource mask to avoid conflict with samplers at the same binding
		bool hasOverlap = (samplerMask & (1 << i)) != 0;
		mapping.resourceMask = hasOverlap ? getResourceTypeMask(resourceTypes[i]) : VK_SPIRV_RESOURCE_TYPE_ALL_EXT;

		mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT;
		mapping.sourceData.pushIndex.heapOffset = descriptorOffset * descriptorSize;
		mapping.sourceData.pushIndex.pushOffset = pushConstantSize;
		mapping.sourceData.pushIndex.heapIndexStride = descriptorSize;
		mapping.sourceData.pushIndex.heapArrayStride = descriptorSize;
		descriptorOffset++;
		printf("binding t%u (%s) -> heap slot %u\n", i, resourceNames[i].c_str(), descriptorOffset - 1);
	}


	// texture array descriptor (set 1)
	{
		VkDescriptorSetAndBindingMappingEXT& mapping = mappings[mappingOffset++];
		mapping.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT;
		mapping.descriptorSet = 1;
		mapping.firstBinding = 0;
		mapping.bindingCount = 1;
		mapping.resourceMask = VK_SPIRV_RESOURCE_TYPE_ALL_EXT;
		mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
		mapping.sourceData.constantOffset.heapArrayStride = descriptorSize;
		mapping.sourceData.constantOffset.heapOffset = DESCRIPTOR_LIMIT * descriptorSize;
	}

	// sampler descriptors (set 2)
	{
		VkDescriptorSetAndBindingMappingEXT& mapping = mappings[mappingOffset++];
		mapping.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT;
		mapping.descriptorSet = 2;
		mapping.firstBinding = 0;
		mapping.bindingCount = DESCRIPTOR_LIMIT_SAMPLERS;
		mapping.resourceMask = VK_SPIRV_RESOURCE_TYPE_ALL_EXT;
		mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
		mapping.sourceData.constantOffset.heapArrayStride = descriptorSize;
	}

	VkShaderDescriptorSetAndBindingMappingInfoEXT result = { VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT };
	result.mappingCount = mappingOffset;
	result.pMappings = mappings;
	return result;
}

Program createProgram(VkDevice device, std::vector<SpvReflectShaderModule*> modules,size_t descriptorSize,size_t pushConstantSize)
{
	Program program = {};

	program.descriptorSize = descriptorSize;

	// gather resources and samplers separately
	VkDescriptorType resourceTypes[32] = {};
	std::string resourceNames[32] = {};
	gatherResources(modules,program.resourceMask,0,resourceTypes,resourceNames);
	memcpy(program.resourceTypes, resourceTypes, sizeof(resourceTypes));

	VkDescriptorType samplerTypes[32] = {};
	std::string samplerNames[32] = {};
	gatherResources(modules,program.samplerMask,2,samplerTypes,samplerNames);

	for (int i = 0; i < 32; ++i){
	    program.resourceNames[i] = resourceNames[i];
		program.samplerNames[i] = samplerNames[i];
	}

	// count push descriptors (non-sampler resources)
	uint32_t pushDescriptorCount = 0;
	for (uint32_t i = 0; i < 32; ++i)
		if ((program.resourceMask & (1 << i)) && resourceTypes[i] != VK_DESCRIPTOR_TYPE_SAMPLER)
			pushDescriptorCount++;

	program.pushDescriptorCount = pushDescriptorCount;
	program.descriptorCount = pushDescriptorCount;

	// compute push constant size from reflection
	size_t computedPushConstantSize = 0;
	for (auto* mod : modules) {
        printf("Module stage %d push constants:\n", mod->shader_stage);
        uint32_t pcCount = 0;
        spvReflectEnumeratePushConstantBlocks(mod,&pcCount, nullptr);
        std::vector<SpvReflectBlockVariable*> blocks(pcCount);
        spvReflectEnumeratePushConstantBlocks(mod,&pcCount, blocks.data());
        for (auto& pc : blocks) {
            computedPushConstantSize += pc->size;
        }
	}

	program.pushConstantSize = uint32_t(computedPushConstantSize);

	assert(program.pushConstantSize == pushConstantSize);

	return program;
}

VkPipeline createGraphicsPipeline(VkDevice device, VkPipelineCache pipelineCache,
                                  const VkPipelineRenderingCreateInfo& renderingInfo,
                                  const Program& program,
                                  std::vector<SpvReflectShaderModule*> modules)
{
	VkPipelineCreateFlags2CreateInfo extraFlags = { VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO };
	extraFlags.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
	extraFlags.pNext = &renderingInfo;

	VkGraphicsPipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	createInfo.pNext = &extraFlags;

	// generate heap mapping from pre-gathered program data
	VkShaderDescriptorSetAndBindingMappingInfoEXT heapMapping = {};
	VkDescriptorSetAndBindingMappingEXT heapMappingTable[34] = {};

	std::string samplerNames[32] = {};
	for (int i = 0; i < 32; ++i)
		samplerNames[i] = program.samplerNames[i];

	heapMapping = generateHeapMapping(
	    program.resourceMask, program.resourceTypes, program.resourceNames,
	    program.samplerMask, samplerNames,
	    program.pushConstantSize, program.descriptorSize,
	    heapMappingTable);

	// shader stages
	std::vector<VkPipelineShaderStageCreateInfo> stages(modules.size());
	std::vector<VkShaderModuleCreateInfo> shaderModules(modules.size());

	for (size_t i = 0; i < modules.size(); ++i)
	{
		auto& mod = modules[i];

		VkShaderModuleCreateInfo& module = shaderModules[i];
		module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		module.codeSize = spvReflectGetCodeSize(mod);
		module.pCode = spvReflectGetCode(mod);
		module.pNext = &heapMapping;

		VkPipelineShaderStageCreateInfo& stage = stages[i];
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = static_cast<VkShaderStageFlagBits>(mod->shader_stage);
		stage.pName = mod->entry_point_name;
		stage.pSpecializationInfo = nullptr;
		stage.pNext = &module;
	}

	createInfo.stageCount = uint32_t(stages.size());
	createInfo.pStages = stages.data();

	// mesh shaders: no vertex input or input assembly
	createInfo.pVertexInputState = nullptr;
	createInfo.pInputAssemblyState = nullptr;

	VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
	createInfo.pViewportState = &viewportState;

	VkPipelineRasterizationStateCreateInfo rasterizationState = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizationState.lineWidth = 1.f;
	rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationState.depthBiasEnable = true;
	createInfo.pRasterizationState = &rasterizationState;

	VkPipelineMultisampleStateCreateInfo multisampleState = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	createInfo.pMultisampleState = &multisampleState;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depthStencilState.depthTestEnable = true;
	depthStencilState.depthWriteEnable = true;
	depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER;
	createInfo.pDepthStencilState = &depthStencilState;

	VkPipelineColorBlendAttachmentState colorAttachmentStates[8] = {};
	assert(renderingInfo.colorAttachmentCount <= 8);
	for (uint32_t i = 0; i < renderingInfo.colorAttachmentCount; ++i){
	    colorAttachmentStates[i].colorWriteMask =
		    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorAttachmentStates[i].blendEnable = VK_FALSE;
	}

	VkPipelineColorBlendStateCreateInfo colorBlendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendState.attachmentCount = renderingInfo.colorAttachmentCount;
	colorBlendState.pAttachments = colorAttachmentStates;
	createInfo.pColorBlendState = &colorBlendState;

	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_DEPTH_BIAS,VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
	};

	VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicState.dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]);
	dynamicState.pDynamicStates = dynamicStates;
	createInfo.pDynamicState = &dynamicState;

	createInfo.layout = VK_NULL_HANDLE;

	VkPipeline pipeline = 0;
	VK_CHECK(vkCreateGraphicsPipelines(device, pipelineCache, 1, &createInfo, 0, &pipeline));

	return pipeline;
}

std::vector<uint32_t> load_spirv(const char *path) {
  std::ifstream file(path, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    return {};
  }

  size_t fileSize = (size_t)file.tellg();
  std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
  file.seekg(0);
  file.read((char *)buffer.data(), fileSize);
  file.close();
  return buffer;
}

VkImageViewCreateInfo getImageViewInfo(VkImage image, VkFormat format, uint32_t mipLevel, uint32_t levelCount)
{
	VkImageAspectFlags aspectMask = (format == VK_FORMAT_D32_SFLOAT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	createInfo.image = image;
	createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createInfo.format = format;
	createInfo.subresourceRange.aspectMask = aspectMask;
	createInfo.subresourceRange.baseMipLevel = mipLevel;
	createInfo.subresourceRange.levelCount = levelCount;
	createInfo.subresourceRange.layerCount = 1;

	return createInfo;
}

void getDescriptor(VkDevice device, VkImage image, VkFormat format, uint32_t mipLevel, uint32_t levelCount, VkDescriptorType type, void* descriptor, size_t descriptorSize)
{
	VkImageViewCreateInfo viewInfo = getImageViewInfo(image, format, mipLevel, levelCount);

	VkImageDescriptorInfoEXT imageInfo = { VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT };
	imageInfo.pView = &viewInfo;
	imageInfo.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkResourceDescriptorInfoEXT resourceInfo = { VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
	resourceInfo.type = type;
	resourceInfo.data.pImage = &imageInfo;

	VkHostAddressRangeEXT descriptorRange = { descriptor, descriptorSize };

	VK_CHECK(vkWriteResourceDescriptorsEXT(device, 1, &resourceInfo, &descriptorRange));
}

void getDescriptor(VkDevice device, VkDeviceAddress address, VkDeviceSize size, VkDescriptorType type, void* descriptor, size_t descriptorSize)
{
	VkDeviceAddressRangeEXT addressRange = { address, size };

	VkResourceDescriptorInfoEXT resourceInfo = { VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
	resourceInfo.type = type;
	resourceInfo.data.pAddressRange = &addressRange;

	VkHostAddressRangeEXT descriptorRange = { descriptor, descriptorSize };

	VK_CHECK(vkWriteResourceDescriptorsEXT(device, 1, &resourceInfo, &descriptorRange));
}

VkSamplerCreateInfo getSamplerInfo(VkFilter filter, VkSamplerMipmapMode mipmapMode, VkSamplerAddressMode addressMode)
{
	VkSamplerCreateInfo createInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	createInfo.magFilter = filter;
	createInfo.minFilter = filter;
	createInfo.mipmapMode = mipmapMode;
	createInfo.addressModeU = addressMode;
	createInfo.addressModeV = addressMode;
	createInfo.addressModeW = addressMode;
	createInfo.minLod = 0;
	createInfo.maxLod = 16.f;
	createInfo.anisotropyEnable = mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR;
	createInfo.maxAnisotropy = mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR ? 4.f : 1.f;

	return createInfo;
}

void getDescriptor(VkDevice device, VkFilter filter, VkSamplerMipmapMode mipmapMode, VkSamplerAddressMode addressMode, VkSamplerReductionModeEXT reductionMode, void* descriptor, size_t descriptorSize)
{
	VkSamplerCreateInfo createInfo = getSamplerInfo(filter, mipmapMode, addressMode);

	VkSamplerReductionModeCreateInfoEXT reductionInfo = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT };
	reductionInfo.reductionMode = reductionMode;

	if (reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT)
		createInfo.pNext = &reductionInfo;

	VkHostAddressRangeEXT descriptorRange = { descriptor, descriptorSize };

	VK_CHECK(vkWriteSamplerDescriptorsEXT(device, 1, &createInfo, &descriptorRange));
}

VkImageMemoryBarrier2 imageBarrier(VkImage image, VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask,
    VkImageLayout oldLayout, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask, VkImageLayout newLayout, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount)
{
	VkImageMemoryBarrier2 result = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };

	result.srcStageMask = srcStageMask;
	result.srcAccessMask = srcAccessMask;
	result.dstStageMask = dstStageMask;
	result.dstAccessMask = dstAccessMask;
	result.oldLayout = oldLayout;
	result.newLayout = newLayout;
	result.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	result.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	result.image = image;
	result.subresourceRange.aspectMask = aspectMask;
	result.subresourceRange.baseMipLevel = baseMipLevel;
	result.subresourceRange.levelCount = levelCount;
	result.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

	return result;
}

void batchBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stageMask, std::initializer_list<VkImage> colorImages,
    std::initializer_list<VkImage> depthImages){
    VkImageMemoryBarrier2 imageBarriers[32];
    assert(colorImages.size() + depthImages.size() <= sizeof(imageBarriers) / sizeof(imageBarriers[0]));

	VkAccessFlags2 accessFlags = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

	size_t imageBarrierCount = 0;
	for (VkImage image : colorImages)
		imageBarriers[imageBarrierCount++] = imageBarrier(image, stageMask, 0,
	        VK_IMAGE_LAYOUT_UNDEFINED, stageMask, accessFlags, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT,0,1);
	for (VkImage image : depthImages)
		imageBarriers[imageBarrierCount++] = imageBarrier(image, stageMask, 0, VK_IMAGE_LAYOUT_UNDEFINED, stageMask, accessFlags,
	        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_DEPTH_BIT,0,1);

	VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependencyInfo.imageMemoryBarrierCount = unsigned(imageBarrierCount);
	dependencyInfo.pImageMemoryBarriers = imageBarriers;

	vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

struct UploadEntry{
    VkBuffer dst;
    const void* data;
    VkDeviceSize size;
};

void batchUpload(VkDevice device, VmaAllocator allocator,
    VkCommandBuffer commandBuffer, VkQueue queue, std::array<UploadEntry, 10> uploads){
    VkDeviceSize totalSize = 0;
    for(auto& upload : uploads){
        totalSize += upload.size;
    }

    Buffer staging = createBuffer(device, allocator, totalSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST);

    char* mapped = static_cast<char*>(staging.info.pMappedData);

    VkDeviceSize offset = 0;
    for (auto& upload : uploads) {
        memcpy(mapped + offset, upload.data, upload.size);
        offset += upload.size;
    }

    VK_CHECK(vmaFlushAllocation(m_vmaAllocator, staging.allocation, 0, totalSize));

    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));
    VkCommandBufferBeginInfo cmdBufferBeginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo));

    offset = 0;
    for (auto& upload : uploads) {
        VkBufferCopy region{};
        region.srcOffset = offset;
        region.dstOffset = 0;
        region.size = upload.size;
        vkCmdCopyBuffer(commandBuffer, staging.buffer, upload.dst, 1, &region);
        offset += upload.size;
    }

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkCommandBufferSubmitInfo commandBufferSubmitInfo =
        createCommandBufferSubmitInfo(commandBuffer);

    VkSubmitInfo2 submitInfo =
        createDrawSubmitInfo(&commandBufferSubmitInfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, nullptr));

    VK_CHECK(vkDeviceWaitIdle(device));

    destroyBuffer(allocator,staging);
}

struct DescriptorInfo
{
	union
	{
		VkDescriptorImageInfo image;
		VkDescriptorBufferInfo buffer;
		VkAccelerationStructureKHR accelerationStructure;
	};

	const void* resource = NULL;
	int resourceMip = -1;

	DescriptorInfo()
	{
	}

	DescriptorInfo(VkAccelerationStructureKHR structure)
	{
		accelerationStructure = structure;
	}

	DescriptorInfo(VkImageView imageView)
	{
		image.sampler = VK_NULL_HANDLE;
		image.imageView = imageView;
		image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	}

	DescriptorInfo(VkSampler sampler)
	{
		image.sampler = sampler;
		image.imageView = VK_NULL_HANDLE;
		image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	DescriptorInfo(VkBuffer buffer_)
	{
		buffer.buffer = buffer_;
		buffer.offset = 0;
		buffer.range = VK_WHOLE_SIZE;
	}

	DescriptorInfo(const struct Buffer& buffer);
	DescriptorInfo(const struct Image& image);
	DescriptorInfo(const struct Image& image, VkImageView mipView, int mipIndex);
};

DescriptorInfo::DescriptorInfo(const struct Buffer& buffer)
    : DescriptorInfo(buffer.buffer)
{
	resource = &buffer;
}

DescriptorInfo::DescriptorInfo(const struct Image& image)
    : DescriptorInfo(image.imageView)
{
	resource = &image;
}

DescriptorInfo::DescriptorInfo(const struct Image& image, VkImageView mipView, int mipIndex)
    : DescriptorInfo(mipView)
{
	resource = &image;
	resourceMip = mipIndex;
}

uint32_t pushDescriptorHeap(FrameDescriptors& framedesc, const Program& program, const DescriptorInfo* descriptors)
{
    //printf("offset %zu + count %zu <= offsetEnd %zu\n",framedesc.descriptorOffset,program.pushDescriptorCount,framedesc.descriptorOffsetEnd);
	assert(framedesc.descriptorOffset + program.pushDescriptorCount <= framedesc.descriptorOffsetEnd);

	uint32_t result = framedesc.descriptorOffset;

	for (int i = 0; i < 32; ++i)
		if (program.resourceMask & (1 << i))
		{
		    //printf("resourceMask: %zu and type: %zu\n",program.resourceMask & (1 << i),program.resourceTypes[i]);
			const auto& info = descriptors[i];

			char descriptor[128];

			switch (program.resourceTypes[i])
			{
			case VK_DESCRIPTOR_TYPE_SAMPLER:
			{
				// mapped via constant offsets (statically)
				continue;
			}
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			{
				const Image* image = static_cast<const Image*>(info.resource);
				assert(image);
				uint32_t mipLevel = (info.resourceMip < 0) ? 0 : uint32_t(info.resourceMip);
				uint32_t levelCount = (info.resourceMip < 0) ? VK_REMAINING_MIP_LEVELS : 1;
				getDescriptor(framedesc.device, image->image, image->imageFormat, mipLevel, levelCount, program.resourceTypes[i], descriptor, framedesc.descriptorSize);
				break;
			}

			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			{
				const Buffer* buffer = static_cast<const Buffer*>(info.resource);
				assert(buffer);
				getDescriptor(framedesc.device, buffer->address, buffer->size, program.resourceTypes[i], descriptor, framedesc.descriptorSize);
				break;
			}

			case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
			{
				VkAccelerationStructureDeviceAddressInfoKHR addressInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
				addressInfo.accelerationStructure = info.accelerationStructure;
				VkDeviceAddress address = vkGetAccelerationStructureDeviceAddressKHR(framedesc.device, &addressInfo);
				getDescriptor(framedesc.device, address, 0, program.resourceTypes[i], descriptor, framedesc.descriptorSize);
				break;
			}
			default:
				assert(!"Unsupported descriptor type");
			}

			memcpy(static_cast<char*>(framedesc.descriptorHeap) + framedesc.descriptorOffset * framedesc.descriptorSize, descriptor, framedesc.descriptorSize);
			framedesc.descriptorOffset++;
		}

	return result;
}

template<size_t PushDescriptors>
void pushDescriptors(VkCommandBuffer commandBuffer, FrameDescriptors& framedesc, const Program& program, const DescriptorInfo (&descriptors)[PushDescriptors])
{
	uint32_t descriptorOffset = pushDescriptorHeap(framedesc, program, descriptors);

	VkPushDataInfoEXT pushDataInfo = { VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT };
	pushDataInfo.offset = program.pushConstantSize;
	pushDataInfo.data.address = &descriptorOffset;
	pushDataInfo.data.size = sizeof(descriptorOffset);

	vkCmdPushDataEXT(commandBuffer, &pushDataInfo);
}

void pushConstants(VkCommandBuffer commandBuffer, FrameDescriptors& framedesc, const Program& program, const TaskConstants& constants)
{
    char pushData[256] = {};

        // Copy TaskConstants into the beginning
        memcpy(pushData, &constants, sizeof(constants));

        // Indices start at pushConstantSize (byte 32), NOT sizeof(TaskConstants) (byte 16)
        uint32_t* indices = (uint32_t*)(pushData + program.pushConstantSize);
        for (uint32_t i = 0; i < program.descriptorCount; i++)
            indices[i] = framedesc.descriptorOffset + i;

        uint32_t totalSize = program.pushConstantSize
            + program.descriptorCount * sizeof(uint32_t);

        for (uint32_t i = 0; i < program.descriptorCount; i++)
            printf("  index[%d] = %d\n", i, indices[i]);

        VkPushDataInfoEXT pushDataInfo = { VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT };
        pushDataInfo.offset = 0;
        pushDataInfo.data.address = pushData;
        pushDataInfo.data.size = totalSize;

        vkCmdPushDataEXT(commandBuffer, &pushDataInfo);
}

template<size_t PushDescriptors>
void pushDescriptorsAndConstants(VkCommandBuffer commandBuffer, FrameDescriptors& framedesc,
                                  const Program& program, const DescriptorInfo (&descriptors)[PushDescriptors],
                                  const TaskConstants& constants)
{
    // Write descriptor data into the heap
    uint32_t baseOffset = pushDescriptorHeap(framedesc, program, descriptors);

    // Build combined push data: [constants] [heap indices]
    char pushData[256] = {};
    memcpy(pushData, &constants, sizeof(constants));

    uint32_t* indices = (uint32_t*)(pushData + program.pushConstantSize);
    for (uint32_t i = 0; i < program.descriptorCount; i++)
        indices[i] = baseOffset + i;

    uint32_t totalSize = program.pushConstantSize
        + program.descriptorCount * sizeof(uint32_t);

    VkPushDataInfoEXT pushDataInfo = { VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT };
    pushDataInfo.offset = 0;
    pushDataInfo.data.address = pushData;
    pushDataInfo.data.size = totalSize;

    vkCmdPushDataEXT(commandBuffer, &pushDataInfo);
}

void setDebugName(VkDevice device, uint64_t handle, VkObjectType type, const char* name)
{
    VkDebugUtilsObjectNameInfoEXT info = {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT
    };
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(device, &info);
}

glm::mat4 perspectiveProjection(float fovY, float aspectWbyH, float zNear)
{
	float f = 1.0f / tanf(fovY / 2.0f);
	return glm::mat4(
	    f / aspectWbyH, 0.0f, 0.0f, 0.0f,
	    0.0f, f, 0.0f, 0.0f,
	    0.0f, 0.0f, 0.0f, 1.0f,
	    0.0f, 0.0f, zNear, 0.0f);
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods){
    if(action == GLFW_PRESS){
        if (key == GLFW_KEY_ESCAPE)
		{
			glfwSetWindowShouldClose(window, true);
		}
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userData)
{
    // Shader printf messages come through as INFO with message ID hash
    // They also contain "DEBUG-PRINTF" in the message ID name
    if (data->pMessageIdName && strstr(data->pMessageIdName, "DEBUG-PRINTF")) {
        printf("[SHADER] %s\n", data->pMessage);
        return VK_FALSE;
    }

    // Print warnings and errors normally
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        fprintf(stderr, "[ERROR] %s\n", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        fprintf(stderr, "[WARN]  %s\n", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        printf("[INFO]  %s\n", data->pMessage);

    return VK_FALSE;
}

void mouseCallback(GLFWwindow* window, int button, int action, int mods)
{
	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		glfwSetCursorPos(window, 0, 0);
	}
	else if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_RIGHT)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

VkPipeline createComputePipeline(VkDevice device, VkPipelineCache pipelineCache,const Program& program, const SpvReflectShaderModule* module){
    assert(static_cast<VkShaderStageFlagBits>(module->shader_stage) == VK_SHADER_STAGE_COMPUTE_BIT);
    VkComputePipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };

    VkPipelineCreateFlags2CreateInfo extraFlags = { VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO };
	extraFlags.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
    createInfo.pNext = &extraFlags;

    VkShaderDescriptorSetAndBindingMappingInfoEXT heapMapping = {};
	VkDescriptorSetAndBindingMappingEXT heapMappingTable[34] = {};

	std::string samplerNames[32] = {};
	for (int i = 0; i < 32; ++i)
		samplerNames[i] = program.samplerNames[i];

	heapMapping = generateHeapMapping(
	    program.resourceMask, program.resourceTypes, program.resourceNames,
	    program.samplerMask, samplerNames,
	    program.pushConstantSize, program.descriptorSize,
	    heapMappingTable);

    VkShaderModuleCreateInfo mod = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mod.codeSize = spvReflectGetCodeSize(module);
    mod.pCode = spvReflectGetCode(module);
    mod.pNext = &heapMapping;

    VkPipelineShaderStageCreateInfo stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	stage.stage = static_cast<VkShaderStageFlagBits>(module->shader_stage);
	stage.pName = "main";
	stage.pSpecializationInfo = nullptr;
	stage.pNext = &module;

	createInfo.stage = stage;
	createInfo.layout = VK_NULL_HANDLE;

	VkPipeline pipeline = 0;
	VK_CHECK(vkCreateComputePipelines(device, pipelineCache, 1, &createInfo, 0, &pipeline));

	return pipeline;
}

struct AliasEntry{
    float probability;
    uint32_t alias;
    float pdf;
    uint32_t _pad;
};

float computePower(const Light& light){
    vec3 effective = vec3(light.color[0]*light.intensity,light.color[1]*light.intensity,
        light.color[2]*light.intensity);
    float lum = 0.2126f*effective.x+0.7152f*effective.y+0.0722f*effective.z;

    float pi = glm::pi<float>();

    switch(light.type){
        case cgltf_light_type_point:
            return 4.f*pi*lum;
        case cgltf_light_type_spot:
            return 2.f*pi*(1.f-light.spotCosOuter)*lum;
        case cgltf_light_type_directional:
            return lum*10.f;
        default:
            return 0.f;
    }
}

void buildAliasTable(const std::vector<float>& weights,std::vector<AliasEntry>& out){
    size_t n = weights.size();
    if(n==0){
        out.clear();
        return;
    }

    float sum = 0.f;
    for(float w : weights){
        sum += w;
    }

    std::vector<float> probs(n);
    for(size_t i=0;i<n;i++){
        probs[i] = (weights[i]*n)/sum;
    }

    std::vector<size_t> small;
    std::vector<size_t> large;
    small.reserve(n);
    large.reserve(n);
    for(size_t i=0;i<n;i++){
        (probs[i]<1.f ? small : large).push_back(i);
    }

    out.resize(n);
    for(size_t i=0;i<n;i++){
        out[i].pdf = weights[i]/sum;
    }

    while(!small.empty()&&!large.empty()){
        size_t s = small.back();
        small.pop_back();
        size_t l = large.back();
        large.pop_back();

        out[s].probability = probs[s];
        out[s].alias = (uint32_t)l;

        probs[l] = (probs[l]+probs[s])-1.f;

        if(probs[l]<1.f){
            small.push_back(l);
        }else{
            large.push_back(l);
        }
    }

    while(!large.empty()){
        size_t l = large.back();
        large.pop_back();

        out[l].probability = 1.f;
        out[l].alias = (uint32_t)l;
    }

    while(!small.empty()){
        size_t s = small.back();
        small.pop_back();

        out[s].probability = 1.f;
        out[s].alias = (uint32_t)s;
    }
}

int main() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window =
      glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Lycaon", nullptr, nullptr);

  glfwSetMouseButtonCallback(window, mouseCallback);
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetMouseButtonCallback(window, mouseCallback);

  vkb::InstanceBuilder builder;

  bool validation_layers = true;
#ifndef _DEBUG
  validation_layers = true;
#endif

  VK_CHECK(volkInitialize());

  auto instance_return = builder.set_app_name("Lycaon")
                             .request_validation_layers(validation_layers)
                             .set_debug_callback(debugCallback)
                             .set_debug_messenger_severity(
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
                             .set_debug_messenger_type(
                                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
                             //.add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT)
                             .add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT)
                             //.add_validation_feature_disable(VK_VALIDATION_FEATURE_DISABLE_SHADERS_EXT)
                             .require_api_version(1, 4, 0)
                             .build();

  vkb::Instance vkb_instance = instance_return.value();
  VkInstance instance = vkb_instance.instance;
  VkDebugUtilsMessengerEXT debug_callback = vkb_instance.debug_messenger;

  VkSurfaceKHR surface;
  VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

  volkLoadInstance(instance);

  VkPhysicalDeviceFeatures features00{};
  features00.samplerAnisotropy = VK_TRUE;

  VkPhysicalDeviceVulkan12Features features12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  features12.bufferDeviceAddress = VK_TRUE;
  features12.descriptorIndexing = VK_TRUE;
  features12.runtimeDescriptorArray = VK_TRUE;
  features12.samplerFilterMinmax = VK_TRUE;
  features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  VkPhysicalDeviceVulkan13Features features13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;
  features13.shaderDemoteToHelperInvocation = VK_TRUE;

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
      .rayTracingPipeline = VK_TRUE};

  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures{
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
      .accelerationStructure = VK_TRUE};

  VkPhysicalDeviceDescriptorHeapFeaturesEXT heapFeatures{};
  heapFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
  heapFeatures.descriptorHeap = VK_TRUE;

  // VkPhysicalDeviceMaintenance5Features maint5Features{};
  // maint5Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES;
  // maint5Features.maintenance5 = VK_TRUE;

  VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
  meshFeatures.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
  meshFeatures.taskShader = VK_TRUE;
  meshFeatures.meshShader = VK_TRUE;

  VkPhysicalDeviceMaintenance5FeaturesKHR maint5 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR
  };
  maint5.maintenance5 = VK_TRUE;

  rtFeatures.pNext = &accelFeatures;
  accelFeatures.pNext = &maint5;
  maint5.pNext = &features00;

  vkb::PhysicalDeviceSelector selector{vkb_instance};
  vkb::PhysicalDevice physicalDevice_return =
      selector.set_minimum_version(1, 4)
          .set_required_features_12(features12)
          .set_required_features_13(features13)
          .set_required_features(features00)
          .add_required_extensions({
              VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
              VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
              VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
              VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
              VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
              VK_EXT_MESH_SHADER_EXTENSION_NAME,
              VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
          })
          .add_required_extension_features(accelFeatures)
          .add_required_extension_features(heapFeatures)
          .add_required_extension_features(meshFeatures)
          .add_required_extension_features(maint5)
          .set_surface(surface)
          .select()
          .value();

  vkb::DeviceBuilder deviceBuilder{physicalDevice_return};
  vkb::Device device_return = deviceBuilder.build().value();

  m_device = device_return.device;
  m_physicalDevice = physicalDevice_return.physical_device;

  volkLoadDevice(m_device);

  vkb::SwapchainBuilder swapchainBuilder{m_physicalDevice, m_device, surface};

  VkFormat swapchainFormat = getSwapchainFormat(m_physicalDevice, surface);

  Swapchain swapchain{};
  createSwapchain(swapchain, swapchainFormat, window, swapchainBuilder, {});

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
  printf("Driver version: %d.%d.%d\n",
      VK_VERSION_MAJOR(props.driverVersion),
      VK_VERSION_MINOR(props.driverVersion),
      VK_VERSION_PATCH(props.driverVersion));
  printf("API version: %d.%d.%d\n",
      VK_API_VERSION_MAJOR(props.apiVersion),
      VK_API_VERSION_MINOR(props.apiVersion),
      VK_API_VERSION_PATCH(props.apiVersion));

  VkQueue graphicsQueue =
      device_return.get_queue(vkb::QueueType::graphics).value();
  uint32_t queueIndex =
      device_return.get_queue_index(vkb::QueueType::graphics).value();

  VkCommandPool commandPools[FRAMES_IN_FLIGHT];
  VkCommandBuffer commandBuffers[FRAMES_IN_FLIGHT];

  for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
    commandPools[i] = createCommandPool(m_device, queueIndex);
    createCommandBuffer(m_device, commandPools[i], commandBuffers[i]);
  }

  VkFence renderFences[FRAMES_IN_FLIGHT];
  VkSemaphore availableSemaphore[FRAMES_IN_FLIGHT];
  std::vector<VkSemaphore> presentSemaphore(swapchain.imageCount);

  for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
    renderFences[i] = createFence(m_device);
    availableSemaphore[i] = createSemaphore(m_device);
  }

  for (int i = 0; i < swapchain.imageCount; i++) {
    presentSemaphore[i] = createSemaphore(m_device);
  }

  std::deque<std::function<void()>> deletionQueue;

  VmaVulkanFunctions vulkanFunctions{};
  vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.physicalDevice = m_physicalDevice;
  allocatorInfo.device = m_device;
  allocatorInfo.instance = instance;
  allocatorInfo.pVulkanFunctions = &vulkanFunctions;
  allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator));

  deletionQueue.push_back([&]() { vmaDestroyAllocator(m_vmaAllocator); });

  VkPhysicalDeviceProperties2 props2 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  VkPhysicalDeviceDescriptorHeapPropertiesEXT descProps{
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT};
  VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
  asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
  descProps.pNext = &asProps;
  props2.pNext = &descProps;
  vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);

  size_t resourceDescriptorSize = std::max(descProps.imageDescriptorSize,descProps.bufferDescriptorSize);
  resourceDescriptorSize = std::max(resourceDescriptorSize,size_t(descProps.samplerDescriptorSize));
  size_t samplerDescriptorSize = resourceDescriptorSize;

  size_t resourceDescriptorCount = DESCRIPTOR_LIMIT+FRAMES_IN_FLIGHT*DESCRIPTOR_LIMIT_FRAME;

  Buffer resourceHeap{};
  Buffer samplerHeap{};
  resourceHeap = createBuffer(m_device, m_vmaAllocator,resourceDescriptorCount*resourceDescriptorSize+descProps.minResourceHeapReservedRange,
      VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT,VMA_MEMORY_USAGE_AUTO);
  samplerHeap = createBuffer(m_device, m_vmaAllocator,DESCRIPTOR_LIMIT_SAMPLERS*descProps.samplerDescriptorSize+descProps.minSamplerHeapReservedRange,
      VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT,VMA_MEMORY_USAGE_AUTO);

  setDebugName(m_device, (uint64_t)resourceHeap.buffer, VK_OBJECT_TYPE_BUFFER, "ResourceHeap");
  setDebugName(m_device, (uint64_t)samplerHeap.buffer, VK_OBJECT_TYPE_BUFFER, "samplerHeap");

  getDescriptor(m_device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE,
		static_cast<char*>(samplerHeap.info.pMappedData) + 0 * samplerDescriptorSize, descProps.samplerDescriptorSize);
  getDescriptor(m_device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE,
		static_cast<char*>(samplerHeap.info.pMappedData) + 1 * samplerDescriptorSize, descProps.samplerDescriptorSize);


  VkCommandPool initCommandPool = createCommandPool(m_device, queueIndex);
  VkCommandBuffer initCommandBuffer = 0;
  createCommandBuffer(m_device, initCommandPool, initCommandBuffer);

  auto task_spv = load_spirv("build/debug/spirv/task.task.spv");

  if(task_spv.empty()){
      fprintf(stderr, "  Failed to load\n\n");
  }

  auto vert_spv = load_spirv("build/Debug/spirv/mesh.mesh.spv");

  if (vert_spv.empty()) {
    fprintf(stderr, "  Failed to load\n\n");
  }

  auto frag_spv = load_spirv("build/Debug/spirv/pixel.frag.spv");

  if (frag_spv.empty()) {
    fprintf(stderr, "  Failed to load\n\n");
  }

  VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

  static const size_t gbufferCount = 4;
  const VkFormat gbufferFormats[gbufferCount] = {
      VK_FORMAT_R8G8B8A8_UNORM,
      VK_FORMAT_R16G16B16A16_SFLOAT,
      VK_FORMAT_R8G8B8A8_UNORM,
      VK_FORMAT_R16G16B16A16_SFLOAT,
  };

  SpvReflectShaderModule taskModule;
  SpvReflectResult refResult = spvReflectCreateShaderModule(task_spv.size()*sizeof(uint32_t), task_spv.data(), &taskModule);
  assert(refResult == SPV_REFLECT_RESULT_SUCCESS);

  SpvReflectShaderModule vertModule;
  refResult = spvReflectCreateShaderModule(vert_spv.size()*sizeof(uint32_t), vert_spv.data(), &vertModule);
  assert(refResult == SPV_REFLECT_RESULT_SUCCESS);

  SpvReflectShaderModule fragModule;
  refResult = spvReflectCreateShaderModule(frag_spv.size()*sizeof(uint32_t), frag_spv.data(), &fragModule);
  assert(refResult == SPV_REFLECT_RESULT_SUCCESS);

  VkPipelineRenderingCreateInfo gbufferInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
  gbufferInfo.colorAttachmentCount = gbufferCount;
  gbufferInfo.pColorAttachmentFormats = gbufferFormats;
  gbufferInfo.depthAttachmentFormat = depthFormat;


  VkPipelineRenderingCreateInfo tempInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
  tempInfo.colorAttachmentCount = 1;
  tempInfo.depthAttachmentFormat = depthFormat;
  tempInfo.pColorAttachmentFormats = &swapchainFormat;

  VkPipelineCache pipelineCache = 0;

  Program graphicsProgram = createProgram(m_device, {&taskModule,&vertModule,&fragModule}, resourceDescriptorSize, sizeof(TaskConstants));
  graphicsProgram.pipeline = createGraphicsPipeline(m_device, pipelineCache, tempInfo, graphicsProgram, {&taskModule,&vertModule,&fragModule});


  spvReflectDestroyShaderModule(&taskModule);
  spvReflectDestroyShaderModule(&vertModule);
  spvReflectDestroyShaderModule(&fragModule);

  deletionQueue.push_back(
      [&]() { vkDestroyPipeline(m_device, graphicsProgram.pipeline, nullptr);});

  deletionQueue.push_back(
      [&]() { vkDestroyCommandPool(m_device, initCommandPool, nullptr); });

  // const char *shedPath = "assets/theShed/the_shed.glb";
  // Scene shedScene = {};
  // bool shedResult = loadGltf(shedPath,shedScene);
  // if (!shedResult) {
  //   assert(!"failed to load shed scene");
  // }

  // printSceneSummary(shedScene);

  const char *scenePath = "assets/sponza/NewSponza_Main_glTF_003.gltf";
  Scene scene = {};
  bool sceneResult = loadGltf(scenePath, scene);
  if (!sceneResult) {
    assert(!"failed to load scene");
  }

  std::string sceneDir(scenePath);
  sceneDir = sceneDir.substr(0,sceneDir.find_last_of("/\\")+1);

  std::string sceneExe(scenePath);
  size_t dotPos = sceneExe.find_last_of(".");
  sceneExe = (dotPos == std::string::npos) ? "" : sceneExe.substr(dotPos + 1);
  printf("exe: %s\n",sceneExe.c_str());

  //printSceneSummary(scene);
  //
  // for(size_t i=0;i<scene.geometry.vertices.size();i++){
  //     const Vertex& vert = scene.geometry.vertices[i];
  //     std::cout << "Vert: " << i << " tu: " << vert.tu<< " tv: " << vert.tv<<std::endl;
  // }

  // std::vector<int> hist(scene.materials.size(), 0);
  //    for (const auto& d : scene.draws) hist[d.materialIndex]++;
  //    for (size_t i = 0; i < hist.size(); ++i)
  //        fprintf(stderr, "materialIndex %zu: %d draws\n", i, hist[i]);
  //    fflush(stderr);
  uint8_t maxV = 0, maxT = 0;
  for(auto& ml : scene.geometry.meshlets){
      maxV = std::max(maxV,ml.vertexCount);
      maxT = std::max(maxT,ml.triangleCount);
  }
  printf("max vertices: %u, max triangles: %u\n", maxV, maxT);
  assert(maxV <= 64);
  assert(maxT <= 124);



  std::vector<float> weights(scene.lights.size());
  for(size_t i=0;i<scene.lights.size();i++){
      weights[i] = computePower(scene.lights[i]);
  }

  std::vector<AliasEntry> aliasTable;
  buildAliasTable(weights, aliasTable);

  //std::vector<DecodedImage> decodedImages(scene.textures.size());
  std::vector<size_t> imageJobs(scene.textures.size());
  std::iota(imageJobs.begin(),imageJobs.end(),0);
  std::vector<ktxTexture2*> ktxTextures(scene.textures.size(), nullptr);

  const bool fromFile = (sceneExe == "gltf");

  double beginImageTime = glfwGetTime();

  std::for_each(std::execution::par,imageJobs.begin(),imageJobs.end(),
      [&](size_t i){
          auto& texture = scene.textures[i];
          std::string texName = sceneDir+texture.uri;
          KTX_error_code ktxRes = ktxTexture2_CreateFromNamedFile(
              texName.c_str(),KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
              &ktxTextures[i]
          );
          //printf("file name: %s and result %d\n",texName.c_str(),ktxRes);
          assert(ktxRes == KTX_SUCCESS);
      });

  std::vector<Image> images;
  images.reserve(ktxTextures.size());
  size_t imageMemory = 0;

  for(uint32_t i=0;i<ktxTextures.size();i++){
      assert(ktxTextures[i] != nullptr);

      Image img = createKTXImage(m_vmaAllocator,m_device,graphicsQueue,
          initCommandBuffer,ktxTextures[i],VK_IMAGE_USAGE_TRANSFER_DST_BIT
          | VK_IMAGE_USAGE_SAMPLED_BIT);

      VkMemoryRequirements memoryRequirements{};
      vkGetImageMemoryRequirements(m_device, img.image, &memoryRequirements);
      imageMemory += memoryRequirements.size;

      images.push_back(img);
  }

  for(auto& kt : ktxTextures){
      if(kt){
          ktxTexture_Destroy(ktxTexture(kt));
      }
  }

  // std::for_each(std::execution::par,imageJobs.begin(),imageJobs.end(),
  //     [&](size_t i){
  //         const auto& tex = scene.textures[i];
  //         DecodedImage& di = decodedImages[i];

  //         int channels = 0;
  //         if(fromFile){
  //             std::string texName = sceneDir+tex.uri;
  //             di.label = texName;
  //             di.pixels = stbi_load(texName.c_str(),
  //                   &di.width, &di.height, &channels, STBI_rgb_alpha);
  //         }else{
  //             di.label = tex.name;
  //             di.pixels = stbi_load_from_memory(tex.data.data(), (int)tex.data.size(),
  //                   &di.width, &di.height, &channels, STBI_rgb_alpha);
  //         }
  //         di.ok = (di.pixels != nullptr);
  //     });

  // std::vector<Image> images;
  // images.reserve(decodedImages.size());
  // size_t imageMemory = 0;

  // for(size_t i=0;i<decodedImages.size();i++){
  //     DecodedImage& di = decodedImages[i];
  //     if(!di.ok){
  //         printf("Failed to load image %s\n", di.label.c_str());
  //         continue;
  //     }
  //     printf("Texture: %s\n",scene.textures[i].name.c_str());
  //     VkExtent3D size = { uint32_t(di.width), uint32_t(di.height), 4 };
  //     Image image = createImage(m_vmaAllocator, m_device, graphicsQueue, initCommandBuffer,
  //             di.pixels, size, VK_FORMAT_BC7_SRGB_BLOCK,
  //             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);

  //     VkMemoryRequirements memoryRequirements{};
  //     vkGetImageMemoryRequirements(m_device, image.image, &memoryRequirements);
  //     imageMemory += memoryRequirements.size;

  //     images.push_back(image);
  //     stbi_image_free(di.pixels);
  //     di.pixels = nullptr;
  // }

  // for(auto& di : decodedImages){
  //     if(di.pixels){
  //         stbi_image_free(di.pixels);
  //     }
  // }

  deletionQueue.push_back([&](){
      for(Image& image : images){
          destroyImage(m_device,m_vmaAllocator,image);
      }
  });

  for(uint32_t i=0;i<images.size();i++){
      if(images[i].image){
          std::string imgname = "texture"+std::to_string(i);
          std::string imgview = "view"+std::to_string(i);
          setDebugName(m_device, (uint64_t)images[i].image, VK_OBJECT_TYPE_IMAGE, imgname.c_str());
          setDebugName(m_device, (uint64_t)images[i].imageView, VK_OBJECT_TYPE_IMAGE_VIEW, imgview.c_str());
      }
  }

  printf("Loaded %d textures (%.2f MB) in %.2f sec\n", int(images.size()), double(imageMemory) / 1e6,glfwGetTime()-beginImageTime);

  for(size_t i=0;i<scene.textures.size();i++){
      void* dst = static_cast<char*>(resourceHeap.info.pMappedData)
                    + DESCRIPTOR_LIMIT * resourceDescriptorSize    // ← texture region base
                    + i * resourceDescriptorSize;
      getDescriptor(m_device, images[i].image,images[i].imageFormat,0,images[i].mipLevels
          ,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,dst,resourceDescriptorSize);
  }

  Camera cam = scene.camera;


  Globals globals = {};

  float aspect  = float(swapchain.width) / float(swapchain.height);
  float farZ    = 100.f;

  // Reversed-Z projection (matches VK_COMPARE_OP_GREATER)
  glm::mat4 view = glm::mat4_cast(glm::inverse(cam.orientation))
                 * glm::translate(glm::mat4(1.f), -cam.position);
  glm::mat4 proj = glm::perspectiveRH_ZO(cam.fovY, aspect, farZ, cam.znear);

  // Vulkan clip space correction (GLM defaults to OpenGL)
  proj[1][1] *= -1.f;

  globals.view        = view;
  globals.proj        = proj;
  globals.viewProj    = proj * view;
  globals.invViewProj = glm::inverse(globals.viewProj);
  globals.cameraPos   = cam.position;

  // Late afternoon sun
  globals.lightCount = scene.lights.size();

  globals.screenSize = {swapchain.width, swapchain.height};
  globals.nearPlane  = cam.znear;
  globals.farPlane   = farZ;

  // lucas when u wake up make the buffers in niagara and depth image stuff
  Buffer meshBuffer = createBuffer(m_device,m_vmaAllocator,scene.meshes.size()*sizeof(Mesh),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
  Buffer matBuffer = createBuffer(m_device,m_vmaAllocator,scene.materials.size()*sizeof(Material),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
  Buffer vertBuffer = createBuffer(m_device,m_vmaAllocator,scene.geometry.vertices.size()*sizeof(Vertex),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
  Buffer indexBuffer = createBuffer(m_device,m_vmaAllocator,scene.geometry.indices.size()*sizeof(uint32_t),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
  Buffer drawBuffer = createBuffer(m_device,m_vmaAllocator,scene.draws.size()*sizeof(MeshDraw),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);;
  Buffer globalBuffer = createBuffer(m_device,m_vmaAllocator,sizeof(globals),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
  Buffer meshletDataBuffer = createBuffer(m_device,m_vmaAllocator,scene.geometry.meshletData.size()*sizeof(uint32_t),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
  Buffer meshletBuffer = createBuffer(m_device,m_vmaAllocator,scene.geometry.meshlets.size()*sizeof(Meshlet),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
  Buffer lightBuffer = createBuffer(m_device,m_vmaAllocator,scene.lights.size()*sizeof(Light),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
  Buffer aliasTableBuffer = createBuffer(m_device,m_vmaAllocator,aliasTable.size()*sizeof(AliasEntry),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

  setDebugName(m_device, (uint64_t)meshBuffer.buffer,VK_OBJECT_TYPE_BUFFER,"meshBuffer");
  setDebugName(m_device, (uint64_t)matBuffer.buffer,VK_OBJECT_TYPE_BUFFER,"matBuffer");
  setDebugName(m_device, (uint64_t)drawBuffer.buffer,VK_OBJECT_TYPE_BUFFER,"drawBuffer");
  setDebugName(m_device, (uint64_t)globalBuffer.buffer,VK_OBJECT_TYPE_BUFFER,"globalBuffer");
  setDebugName(m_device, (uint64_t)meshletDataBuffer.buffer,VK_OBJECT_TYPE_BUFFER,"meshletDataBuffer");
  setDebugName(m_device, (uint64_t)meshletBuffer.buffer,VK_OBJECT_TYPE_BUFFER,"meshletBuffer");
  setDebugName(m_device, (uint64_t)vertBuffer.buffer,VK_OBJECT_TYPE_BUFFER,"vertBuffer");
  setDebugName(m_device, (uint64_t)indexBuffer.buffer,VK_OBJECT_TYPE_BUFFER,"indexBuffer");
  setDebugName(m_device, (uint64_t)lightBuffer.buffer,VK_OBJECT_TYPE_BUFFER,"lightBuffer");
  setDebugName(m_device, (uint64_t)aliasTableBuffer.buffer,VK_OBJECT_TYPE_BUFFER,"aliasTableBuffer");

  std::array<UploadEntry, 10> uploads = {{
      { meshBuffer.buffer,       scene.meshes.data(),              scene.meshes.size() * sizeof(Mesh) },
      { matBuffer.buffer,        scene.materials.data(),           scene.materials.size() * sizeof(Material) },
      { vertBuffer.buffer,       scene.geometry.vertices.data(),   scene.geometry.vertices.size() * sizeof(Vertex) },
      { indexBuffer.buffer,      scene.geometry.indices.data(),    scene.geometry.indices.size() * sizeof(uint32_t) },
      { drawBuffer.buffer,       scene.draws.data(),               scene.draws.size() * sizeof(MeshDraw) },
      { globalBuffer.buffer,     &globals,                         sizeof(globals) },
      { meshletDataBuffer.buffer,scene.geometry.meshletData.data(),scene.geometry.meshletData.size() * sizeof(uint32_t) },
      { meshletBuffer.buffer,    scene.geometry.meshlets.data(),   scene.geometry.meshlets.size() * sizeof(Meshlet) },
      { lightBuffer.buffer,    scene.lights.data(),   scene.lights.size() * sizeof(Light) },
      { aliasTableBuffer.buffer,    aliasTable.data(),   aliasTable.size() * sizeof(AliasEntry) },
  }};

  batchUpload(m_device, m_vmaAllocator,initCommandBuffer,
      graphicsQueue,uploads);

  std::vector<VkAccelerationStructureKHR> blas;
  std::vector<VkDeviceAddress> blasAddresses;
  VkAccelerationStructureKHR tlas = nullptr;
  bool tlasNeedsRebuild = true;
  Buffer blasBuffer = {};
  Buffer tlasBuffer = {};
  Buffer tlasStagingBuffer = {};
  Buffer tlasInstanceBuffer = {};

  std::vector<VkDeviceSize> compactedSizes;
  buildBLAS(m_device,m_vmaAllocator,scene.meshes,vertBuffer,indexBuffer,
      blas,compactedSizes,blasBuffer,initCommandPool,initCommandBuffer,graphicsQueue,asProps);
  compactBLAS(m_device,m_vmaAllocator,blas,compactedSizes,blasBuffer,initCommandPool,
      initCommandBuffer,graphicsQueue);

  blasAddresses.resize(blas.size());
  printf("blas count: %ul\n", blas.size());
  for(size_t i=0;i<blas.size();i++){
    VkAccelerationStructureDeviceAddressInfoKHR info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    info.accelerationStructure = blas[i];
    blasAddresses[i] = vkGetAccelerationStructureDeviceAddressKHR(m_device, &info);
  }
  printf("obtained blas addresses\n");
  tlasInstanceBuffer = createBuffer(m_device,m_vmaAllocator,sizeof(VkAccelerationStructureInstanceKHR)*scene.draws.size(),
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,VMA_MEMORY_USAGE_AUTO);

  printf("made tlas instances\n");
  for(size_t i=0;i<scene.draws.size();i++){
      const MeshDraw& draw = scene.draws[i];
      assert(draw.meshIndex < blas.size());

      VkAccelerationStructureInstanceKHR instance = {};
      fillInstanceRT(instance, draw, uint32_t(i), blasAddresses[draw.meshIndex]);

      memcpy(static_cast<VkAccelerationStructureInstanceKHR*>(tlasInstanceBuffer.info.pMappedData) + i, &instance, sizeof(VkAccelerationStructureInstanceKHR));
  }
  printf("completed fill instace of priors\n");
  tlas = createTLAS(m_device,m_vmaAllocator,tlasStagingBuffer,tlasInstanceBuffer,scene.draws.size(),tlasBuffer,asProps);
  printf("made tlas\n");
  deletionQueue.push_back([&](){
      destroyBuffer(m_vmaAllocator, meshBuffer);
      destroyBuffer(m_vmaAllocator, matBuffer);
      destroyBuffer(m_vmaAllocator, vertBuffer);
      destroyBuffer(m_vmaAllocator, indexBuffer);
      destroyBuffer(m_vmaAllocator, drawBuffer);
      destroyBuffer(m_vmaAllocator, globalBuffer);
      destroyBuffer(m_vmaAllocator, meshletBuffer);
      destroyBuffer(m_vmaAllocator, meshletDataBuffer);
      destroyBuffer(m_vmaAllocator, lightBuffer);
      destroyBuffer(m_vmaAllocator, aliasTableBuffer);
      destroyBuffer(m_vmaAllocator, tlasBuffer);
      destroyBuffer(m_vmaAllocator, blasBuffer);
      destroyBuffer(m_vmaAllocator, tlasStagingBuffer);
      destroyBuffer(m_vmaAllocator, tlasInstanceBuffer);
      vkDestroyAccelerationStructureKHR(m_device,tlas,0);
      for(VkAccelerationStructureKHR as : blas){
          vkDestroyAccelerationStructureKHR(m_device,as,0);
      }
  });


  Image gbufferTargets[gbufferCount] = {};
  Image depthTargets[FRAMES_IN_FLIGHT] = {};

  double elapsedTime = glfwGetTime();

  printf("size of global: %zu\n",sizeof(globals));

  for (size_t i = 0; i < scene.draws.size(); ++i) {
      fprintf(stderr, "draw[%zu] mesh=%u mat=%u pos=(%.2f,%.2f,%.2f)\n",
              i, scene.draws[i].meshIndex, scene.draws[i].materialIndex,
              scene.draws[i].position.x, scene.draws[i].position.y,
              scene.draws[i].position.z);
  }
  fflush(stderr);

  int frameIndex = 0;
  while (!glfwWindowShouldClose(window)) {
    double currTime = glfwGetTime();
    float dt = float(currTime-elapsedTime);
    elapsedTime = currTime;

    glfwPollEvents();

    if(glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_DISABLED){
        double xpos;
        double ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        vec2 cameraMotion = vec2(glfwGetKey(window,GLFW_KEY_W), glfwGetKey(window, GLFW_KEY_D)) - vec2(glfwGetKey(window, GLFW_KEY_S), glfwGetKey(window, GLFW_KEY_A));
        vec2 cameraRotation = vec2(xpos,ypos);

        float cameraMotionSpeed = 3.0f;
        float cameraRotationSpeed = glm::radians(10.f);

        cam.position += float(cameraMotion.y * dt * cameraMotionSpeed) * (cam.orientation * vec3(1, 0, 0));
		cam.position += float(cameraMotion.x * dt * cameraMotionSpeed) * (cam.orientation * vec3(0, 0, -1));
		cam.orientation = glm::rotate(glm::quat(0, 0, 0, 1), float(-cameraRotation.x * dt * cameraRotationSpeed), vec3(0, 1, 0)) * cam.orientation;
		cam.orientation = glm::rotate(glm::quat(0, 0, 0, 1), float(-cameraRotation.y * dt * cameraRotationSpeed), cam.orientation * vec3(1, 0, 0)) * cam.orientation;

		glfwSetCursorPos(window,0,0);
    }

    SwapchainStatus swapchainStatus = updateSwapchain(
        swapchain, m_device, window, swapchainBuilder, swapchainFormat);
    if (swapchainStatus == Swapchain_NotReady) {
      continue;
    }

    if (swapchainStatus == Swapchain_Resized || !depthTargets[0].image) {
      printf("Swapchain resized: %dx%d\n", swapchain.width, swapchain.height);

      for(Image& img : gbufferTargets){
          if(img.image){
              destroyImage(m_device, m_vmaAllocator, img);
          }
      }

      for(Image& img : depthTargets)
        if(img.image){
            destroyImage(m_device,m_vmaAllocator,img);
        }

      for(size_t i=0;i<gbufferCount;i++){
          gbufferTargets[i] = createImage(m_vmaAllocator,m_device,VkExtent3D(swapchain.width,swapchain.height,1),
              gbufferFormats[i],VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,false);
      }
      for(Image& img : depthTargets)
        img = createImage(m_vmaAllocator,m_device,VkExtent3D(swapchain.width,swapchain.height,1),depthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);


      for(uint32_t i = 0;i<swapchain.imageViews.size();i++){
          if(swapchain.imageViews[i]){
              vkDestroyImageView(m_device, swapchain.imageViews[i],0);
          }
          VkImageViewCreateInfo ivinfo = createImageViewInfo(swapchainFormat, swapchain.images[i], VK_IMAGE_ASPECT_COLOR_BIT);
          VK_CHECK(vkCreateImageView(m_device,&ivinfo,nullptr,&swapchain.imageViews[i]));
      }

    }

    VkResult fenceResult = vkWaitForFences(m_device, 1, &renderFences[frameIndex], VK_TRUE,
                             UINT64_MAX);
    if(fenceResult == VK_ERROR_DEVICE_LOST){
        assert(!"device lost\n");
    }

    FrameDescriptors frameDesc = {};
    frameDesc.device = m_device;
	frameDesc.descriptorHeap = resourceHeap.info.pMappedData;
	frameDesc.descriptorSize = resourceDescriptorSize;
	frameDesc.descriptorOffset = (frameIndex) * DESCRIPTOR_LIMIT_FRAME;
	frameDesc.descriptorOffsetEnd = frameDesc.descriptorOffset + DESCRIPTOR_LIMIT_FRAME;

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        m_device, swapchain.swapchain, UINT64_MAX,
        availableSemaphore[frameIndex], VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
      swapchain.bad = true;
      continue;
    }

    VK_CHECK(acquireResult);

    VK_CHECK(vkResetFences(m_device, 1, &renderFences[frameIndex]));

    Image& depthTarget = depthTargets[frameIndex];

    VkCommandBuffer commandBuffer = commandBuffers[frameIndex];
    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

    VkCommandBufferBeginInfo cmdBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo));


    globals.cameraPos = cam.position;

    globals.cameraPos = cam.position;

    glm::mat4 view = glm::mat4_cast(cam.orientation);
    view[3] = vec4(cam.position,1.0f);
    view = inverse(view);
    view = glm::scale(glm::identity<glm::mat4>(), vec3(1, 1, -1)) * view;

    glm::mat4 proj = perspectiveProjection(cam.fovY, float(swapchain.width)/float(swapchain.height), cam.znear);

    globals.proj = proj;
    globals.view = view;
    globals.viewProj = proj*view;
    globals.invViewProj = inverse(globals.viewProj);




    VkBufferMemoryBarrier2 preBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
    preBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT
                             | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT
                             | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                             | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    preBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                             | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    preBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    preBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    preBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.buffer = globalBuffer.buffer;
    preBarrier.size   = VK_WHOLE_SIZE;

    VkDependencyInfo preDep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    preDep.bufferMemoryBarrierCount = 1;
    preDep.pBufferMemoryBarriers    = &preBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &preDep);

    vkCmdUpdateBuffer(commandBuffer, globalBuffer.buffer, 0, sizeof(Globals), &globals);

    VkBufferMemoryBarrier2 postBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
    postBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
    postBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    postBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT
                              | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT
                              | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    postBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    postBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postBarrier.buffer = globalBuffer.buffer;
    postBarrier.size   = VK_WHOLE_SIZE;

    VkDependencyInfo postDep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    postDep.bufferMemoryBarrierCount = 1;
    postDep.pBufferMemoryBarriers    = &postBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &postDep);

    VkBindHeapInfoEXT bindSamplerHeap = { VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT };
	bindSamplerHeap.heapRange.address = samplerHeap.address;
	bindSamplerHeap.heapRange.size = samplerHeap.info.size;
	bindSamplerHeap.reservedRangeOffset = samplerHeap.info.size - descProps.minSamplerHeapReservedRange;
	bindSamplerHeap.reservedRangeSize = descProps.minSamplerHeapReservedRange;

	VkBindHeapInfoEXT bindResourceHeap = { VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT };
	bindResourceHeap.heapRange.address = resourceHeap.address;
	bindResourceHeap.heapRange.size = resourceHeap.info.size;
	bindResourceHeap.reservedRangeOffset = resourceHeap.info.size - descProps.minResourceHeapReservedRange;
	bindResourceHeap.reservedRangeSize = descProps.minResourceHeapReservedRange;

	vkCmdBindSamplerHeapEXT(commandBuffer, &bindSamplerHeap);
	vkCmdBindResourceHeapEXT(commandBuffer, &bindResourceHeap);

	// batchBarrier(commandBuffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
 //        {gbufferTargets[0].image,gbufferTargets[1].image},{depthTarget.image});
    transitionImage(commandBuffer,depthTarget.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	transitionImage(commandBuffer, swapchain.images[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	// transitionImage(commandBuffer, gbufferTargets[0].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	// transitionImage(commandBuffer, gbufferTargets[1].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	// transitionImage(commandBuffer, gbufferTargets[2].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	// transitionImage(commandBuffer, gbufferTargets[3].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkClearColorValue colorClear = { 135.f / 255.f, 206.f / 255.f, 250.f / 255.f, 15.f / 255.f };
	VkClearDepthStencilValue depthClear = { 0.f, 0 };

 //    VkRenderingAttachmentInfo gbufferAttachments[gbufferCount] = {};
	// for (uint32_t i = 0; i < gbufferCount; ++i){
	// 	gbufferAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	// 	gbufferAttachments[i].imageView = gbufferTargets[i].imageView;
	// 	gbufferAttachments[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	// 	gbufferAttachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	// 	gbufferAttachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	// 	gbufferAttachments[i].clearValue.color = colorClear;
	// }

	VkRenderingAttachmentInfo colorAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView   = swapchain.imageViews[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = colorClear;

  //   VkRenderingAttachmentInfo gbufferAttachments[gbufferCount] = {};
  //   for(uint32_t i=0;i<gbufferCount;i++){
  //      	gbufferAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		// gbufferAttachments[i].imageView = gbufferTargets[i].imageView;
		// gbufferAttachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		// gbufferAttachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		// gbufferAttachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		// gbufferAttachments[i].clearValue.color = colorClear;
  //   }

	VkRenderingAttachmentInfo depthAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	depthAttachment.imageView = depthTarget.imageView;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.clearValue.depthStencil = depthClear;

	VkRenderingInfo passInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
	passInfo.renderArea.extent.width = swapchain.width;
	passInfo.renderArea.extent.height = swapchain.height;
	passInfo.layerCount = 1;
	passInfo.colorAttachmentCount = 1;
	passInfo.pColorAttachments = &colorAttachment;
	passInfo.pDepthAttachment = &depthAttachment;

	vkCmdBeginRendering(commandBuffer, &passInfo);

	VkViewport viewport = { 0, float(swapchain.height), float(swapchain.width), -float(swapchain.height), 0, 1 };
	VkRect2D scissor = { { 0, 0 }, { uint32_t(swapchain.width), uint32_t(swapchain.height) } };

	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
	vkCmdSetDepthBias(commandBuffer, 0, 0, 0);

	vkCmdSetDepthTestEnable(commandBuffer, VK_TRUE);
    vkCmdSetDepthWriteEnable(commandBuffer, VK_TRUE);
    vkCmdSetDepthCompareOp(commandBuffer, VK_COMPARE_OP_GREATER);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,graphicsProgram.pipeline);

	// do descriptor stuff

	for(uint32_t i =0;i<scene.draws.size();i++){
	    MeshDraw& draw = scene.draws[i];
		Mesh& mesh = scene.meshes[draw.meshIndex];

		uint32_t lodIndex = 0;
		MeshLod& lod = mesh.lods[lodIndex];

	    TaskConstants push{};
		push.drawIndex = i;
		push.meshletOffset = lod.meshletOffset;
		push.meshletCount = lod.meshletCount;

		DescriptorInfo descriptors[] = {globalBuffer,vertBuffer,meshletDataBuffer
		,meshletBuffer,meshBuffer,drawBuffer,matBuffer,lightBuffer,aliasTableBuffer};

		pushDescriptorsAndConstants(commandBuffer, frameDesc, graphicsProgram,
                            descriptors, push);
		uint32_t groups = (lod.meshletCount+TASK_GROUP_SIZE-1)/TASK_GROUP_SIZE;

		assert(graphicsProgram.pipeline != VK_NULL_HANDLE);

        vkCmdDrawMeshTasksEXT(commandBuffer, groups, 1, 1);
	}

	vkCmdEndRendering(commandBuffer);

	// transitionImage(commandBuffer, gbufferTargets[0].image,
 //    VK_IMAGE_LAYOUT_GENERAL,
 //    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	transitionImage(commandBuffer, swapchain.images[imageIndex],
	VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	// copyImageToImage(commandBuffer, gbufferTargets[0].image,
 //    swapchain.images[imageIndex],
 //    VkExtent2D(swapchain.width, swapchain.height),
 //    VkExtent2D(swapchain.width, swapchain.height));

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkCommandBufferSubmitInfo commandBufferSubmitInfo =
        createCommandBufferSubmitInfo(commandBuffer);
    VkSemaphoreSubmitInfo waitInfo = createSemaphoreSubmitInfo(
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        availableSemaphore[frameIndex]);
    VkSemaphoreSubmitInfo signalInfo = createSemaphoreSubmitInfo(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, presentSemaphore[imageIndex]);
    VkSubmitInfo2 submitInfo =
        createDrawSubmitInfo(&commandBufferSubmitInfo, &signalInfo, &waitInfo);

    VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submitInfo,
                            renderFences[frameIndex]));

    VkPresentInfoKHR presentInfo{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.swapchain;
    presentInfo.pWaitSemaphores = &presentSemaphore[imageIndex];
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &imageIndex;

    VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));

    frameIndex = (frameIndex + 1) % FRAMES_IN_FLIGHT;
  }

  vkDeviceWaitIdle(m_device);

  for (Image& image : gbufferTargets) {
    if (image.image){
        destroyImage(m_device, m_vmaAllocator, image);
    }
  }
  for(Image& img : depthTargets)
    if (img.image)
        destroyImage(m_device, m_vmaAllocator, img);

  destroyBuffer(m_vmaAllocator,resourceHeap);
  destroyBuffer(m_vmaAllocator,samplerHeap);

  for (auto del = deletionQueue.rbegin(); del != deletionQueue.rend(); del++) {
    (*del)();
  }

  for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
    vkDestroyFence(m_device, renderFences[i], nullptr);
    vkDestroySemaphore(m_device, availableSemaphore[i], nullptr);
  }

  for (int i = 0; i < swapchain.imageCount; i++)
    vkDestroySemaphore(m_device, presentSemaphore[i], nullptr);

  for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
    vkDestroyCommandPool(m_device, commandPools[i], nullptr);

  vkDestroySwapchainKHR(m_device, swapchain.swapchain, nullptr);
  for (VkImageView view : swapchain.imageViews)
    vkDestroyImageView(m_device, view, nullptr);

  vkDestroyDevice(m_device, nullptr);
  vkb::destroy_debug_utils_messenger(instance, debug_callback);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
