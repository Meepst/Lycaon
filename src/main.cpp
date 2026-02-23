#include <cstdint>
#define VMA_IMPLEMENTATION
#define VOLK_IMPLEMENTATION
#include "volk.h"
#include "common.h"
#include "resources.h"
#include "descriptors.h"
#include "scene.h"
#include "swapchain.h"
#include <deque>
#include <functional>
#include <stb_image.h>
#include <iostream>

struct alignas(16) PushConstants{
    uint32_t frameID;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t maxBounces;
    uint32_t tlasIndex;
    uint32_t environmentMapID;
    uint32_t accumImageID;
    uint32_t ouputImageID;

    VkDeviceAddress vertices;
    VkDeviceAddress indices;
    VkDeviceAddress materials;
    VkDeviceAddress instances;
    VkDeviceAddress camera;
    VkDeviceAddress environmentMap;
    VkDeviceAddress rays;
    VkDeviceAddress pathStates;
    VkDeviceAddress hitInfos;
    VkDeviceAddress rayCounts;
    VkDeviceAddress shadowRays;
    VkDeviceAddress shadowContribs;
};

struct Mesh{
    Buffer vertexBuffer;
    Buffer indexBuffer;
    VkDeviceAddress vertexBufferAddress;
    VkDeviceAddress indexBufferAddress;
};

struct alignas(16) Ray {
    glm::vec3 origin;
    glm::vec3 direction;
    float tMin;
    float tMax;
};

struct alignas(16) InstanceData{
    glm::mat4 transform;
    glm::mat4 transformInvTranspose;
    uint32_t matID;
    uint32_t vertOffset;
    uint32_t indexOffset;
};

struct alignas(16) PathState{
    glm::vec3 throughput;
    uint32_t pixelID;
    glm::vec3 radiance;
    uint32_t bounceCount;
    uint32_t rngState;
    uint32_t flags;
};

struct alignas(16) HitInfo{
    glm::vec3 position;
    uint32_t materialID;
    glm::vec3 normal;
    glm::vec2 uv;
    float t;
    uint32_t primitiveID;
    uint32_t instanceID;
    uint32_t flags;
};

struct alignas(16) Camera{
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
    glm::vec3 position;
    float fov;
    glm::vec3 forward;
    float aperture;
    glm::vec3 right;
    float focusDistance;
    glm::vec3 up;
};

struct Environment{
    glm::vec3 sunDirection;
    float sunIntensity;
    glm::vec3 sunColor;
    float environmentIntensity;
};

struct alignas(16) RayCount{
    uint32_t rayCount;
    uint32_t shaderRayCount;
    uint32_t extensionRayCount;
};

// Globals
VkPhysicalDevice m_physicalDevice;
VkDevice m_device;
VmaAllocator m_vmaAllocator;


void drawBackground(VkCommandBuffer commandBuffer, VkImage image){
    VkClearColorValue clearValue{};
    clearValue = {{0.2f,0.3f,0.4f,1.0f}};

    VkImageSubresourceRange clearRange = createImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

    vkCmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
}

Mesh uploadMesh(VkDevice device, VkQueue queue, VkCommandBuffer commandBuffer, VmaAllocator allocator, std::span<uint32_t> indices, std::span<Vertex> vertices){
    const size_t vertexBufferSize = vertices.size()*sizeof(Vertex);
    const size_t indexBufferSize = indices.size()*(sizeof(uint32_t));

    Mesh newMesh;

    newMesh.vertexBuffer = createBuffer(allocator, vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    VkBufferDeviceAddressInfo deviceAddrInfo{.sType=VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    deviceAddrInfo.buffer = newMesh.vertexBuffer.buffer;
    newMesh.vertexBufferAddress = vkGetBufferDeviceAddress(device, &deviceAddrInfo);

    newMesh.indexBuffer = createBuffer(allocator, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    deviceAddrInfo.buffer = newMesh.indexBuffer.buffer;
    newMesh.indexBufferAddress = vkGetBufferDeviceAddress(device, &deviceAddrInfo);

    Buffer stagingBuffer = createBuffer(allocator, vertexBufferSize+indexBufferSize,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VMA_MEMORY_USAGE_CPU_ONLY);

    void* data = stagingBuffer.allocation->GetMappedData();

    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy((char*)data+vertexBufferSize,indices.data(),indexBufferSize);

    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

    VkCommandBufferBeginInfo cmdBufferBeginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo));

    VkBufferCopy vertexCopy{ 0 };
    vertexCopy.dstOffset = 0;
    vertexCopy.srcOffset = 0;
    vertexCopy.size = vertexBufferSize;

    vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer,newMesh.vertexBuffer.buffer, 1, &vertexCopy);

    VkBufferCopy indexCopy{ 0 };
    indexCopy.dstOffset = 0;
    indexCopy.srcOffset = vertexBufferSize;
    indexCopy.size = indexBufferSize;

    vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, newMesh.indexBuffer.buffer, 1, &indexCopy);
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkCommandBufferSubmitInfo subInfo = createCommandBufferSubmitInfo(commandBuffer);
    VkSubmitInfo2 submitInfo = createDrawSubmitInfo(&subInfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, nullptr));
    vkDeviceWaitIdle(device); // should change to proper synchronization

    destroyBuffer(allocator, stagingBuffer);

    return newMesh;
}

Buffer uploadBuffer(VkDevice device, VkQueue queue, VkCommandBuffer commandBuffer, VmaAllocator allocator,
    VkBufferUsageFlags usage, void* data, VkDeviceSize size){

    Buffer newBuffer = createBuffer(allocator, size, usage, VMA_MEMORY_USAGE_GPU_ONLY);

    Buffer stagingBuffer = createBuffer(allocator, size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VMA_MEMORY_USAGE_CPU_ONLY);
    void* scratch = stagingBuffer.allocation->GetMappedData();

    memcpy(scratch, data, size);

    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

    VkCommandBufferBeginInfo cmdBufferBeginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo));

    VkBufferCopy bufferCopy{ 0 };
    bufferCopy.dstOffset = 0;
    bufferCopy.srcOffset = 0;
    bufferCopy.size = size;

    vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer,newBuffer.buffer, 1, &bufferCopy);
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkCommandBufferSubmitInfo subInfo = createCommandBufferSubmitInfo(commandBuffer);
    VkSubmitInfo2 submitInfo = createDrawSubmitInfo(&subInfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, nullptr));
    vkDeviceWaitIdle(device); // should change to proper synchronization

    destroyBuffer(allocator, stagingBuffer);
    return newBuffer;
}

VkPipeline createComputePipeline(VkShaderModule module){

    VkShaderDescriptorSetAndBindingMappingInfoEXT mappingInfo{};
    mappingInfo.sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT;
    mappingInfo.mappingCount = static_cast<uint32_t>(mappings.size());
    mappingInfo.pMappings = mappings.data();

    VkPipelineShaderStageCreateInfo stageCreateInfo{};
    stageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageCreateInfo.module = module;
    stageCreateInfo.pName = "main";

    VkPipelineCreateFlags2CreateInfoKHR flags2{};
    flags2.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR;
    flags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;

    VkComputePipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stage = stageCreateInfo;
    pipelineCreateInfo.layout = VK_NULL_HANDLE;
    pipelineCreateInfo.pNext = &flags2;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo,nullptr,&pipeline));

    return pipeline;
}


int main(){
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Lycaon", nullptr, nullptr);

    vkb::InstanceBuilder builder;

    bool validation_layers = false;
    #ifndef _DEBUG
        validation_layers = true;
    #endif

    auto instance_return = builder.set_app_name("Lycaon")
        .request_validation_layers(validation_layers)
        .use_default_debug_messenger()
        .require_api_version(1,4,0)
        .build();

    vkb::Instance vkb_instance = instance_return.value();
    VkInstance instance = vkb_instance.instance;
    VkDebugUtilsMessengerEXT debug_callback = vkb_instance.debug_messenger;

    VkSurfaceKHR surface;
    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

    VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .rayTracingPipeline = VK_TRUE
    };

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .accelerationStructure = VK_TRUE
    };

    VkPhysicalDeviceDescriptorHeapFeaturesEXT heapFeatures{};
    heapFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
    heapFeatures.descriptorHeap = VK_TRUE;
    heapFeatures.descriptorHeapCaptureReplay = VK_TRUE;

    rtFeatures.pNext = &accelFeatures;
    accelFeatures.pNext = &heapFeatures;

    vkb::PhysicalDeviceSelector selector{vkb_instance};
    vkb::PhysicalDevice physicalDevice_return = selector
        .set_minimum_version(1, 4)
        .set_required_features_12(features12)
        .set_required_features_13(features13)
        .add_required_extensions({
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
        })
        .add_required_extension_features(rtFeatures)
        .set_surface(surface)
        .select().value();

    vkb::DeviceBuilder deviceBuilder{physicalDevice_return};
    vkb::Device device_return = deviceBuilder.build().value();

    m_device = device_return.device;
    m_physicalDevice = physicalDevice_return.physical_device;

    VK_CHECK(volkInitialize());
    volkLoadInstance(instance);
    volkLoadDevice(m_device);

    vkb::SwapchainBuilder swapchainBuilder{m_physicalDevice, m_device, surface};

    VkFormat swapchainFormat = getSwapchainFormat(m_physicalDevice, surface);

    Swapchain swapchain{};
    createSwapchain(swapchain, swapchainFormat,window,swapchainBuilder,{});

    VkQueue graphicsQueue =  device_return.get_queue(vkb::QueueType::graphics).value();
    uint32_t queueIndex = device_return.get_queue_index(vkb::QueueType::graphics).value();

    VkCommandPool commandPools[FRAMES_IN_FLIGHT];
    VkCommandBuffer commandBuffers[FRAMES_IN_FLIGHT];

    for(int i=0;i<FRAMES_IN_FLIGHT;i++){
        commandPools[i] = createCommandPool(m_device, queueIndex);
        createCommandBuffer(m_device, commandPools[i], commandBuffers[i]);
    }

    VkFence renderFences[FRAMES_IN_FLIGHT];
    VkSemaphore availableSemaphore[FRAMES_IN_FLIGHT];
    std::vector<VkSemaphore> presentSemaphore(swapchain.imageCount);

    for(int i=0;i<FRAMES_IN_FLIGHT;i++){
        renderFences[i] = createFence(m_device);
        availableSemaphore[i] = createSemaphore(m_device);
    }

    for(int i=0;i<swapchain.imageCount;i++){
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

    deletionQueue.push_back([&](){vmaDestroyAllocator(m_vmaAllocator);});

    VkExtent3D drawImageExtent = {swapchain.width,swapchain.height,1};
    VkExtent2D drawExtent = {swapchain.width,swapchain.height};
    VkImageUsageFlags drawImageUsages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    Image drawImage = createImage(m_vmaAllocator, m_device, drawImageExtent, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageUsages, false);

    deletionQueue.push_back([&](){
        destroyImage(m_device, m_vmaAllocator, drawImage);
    });

    VkCommandPool initCommandPool = createCommandPool(m_device, queueIndex);
	VkCommandBuffer initCommandBuffer = 0;
	createCommandBuffer(m_device, initCommandPool, initCommandBuffer);


    VkShaderModule compModule = 0;
    loadShaderModule("spirv/test.comp.spirv", m_device, &compModule);

    VkPipeline compPipeline = createComputePipeline(compModule, bindings, cfg);

    std::vector<VkImageViewCreateInfo> textureViews(256);
    for (auto& v : textureViews) {
        v.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        v.image    = /* your VkImage */ VK_NULL_HANDLE;
        v.viewType = VK_IMAGE_VIEW_TYPE_2D;
        v.format   = VK_FORMAT_R8G8B8A8_SRGB;
        v.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
    }


	constexpr size_t pathSegmentSize = SCREEN_WIDTH*SCREEN_HEIGHT*64;
	Buffer pathSegmentBuffer = createBuffer(m_vmaAllocator, pathSegmentSize, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	deletionQueue.push_back([&](){
	    destroyBuffer(m_vmaAllocator,pathSegmentBuffer);
        vkDestroyCommandPool(m_device, initCommandPool, nullptr);
	});

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<Material> materials;
	std::vector<std::string> texturePaths;

	const char* scenePath = "assets/barramundiFish/glTF/BarramundiFish.gltf";
	if(!parseScene(scenePath,vertices,indices,materials,texturePaths)){
	    printf("failed to load gltf scene\n");
	}

    Mesh mesh = uploadMesh(m_device, graphicsQueue, initCommandBuffer, m_vmaAllocator, indices, vertices);
    VkBufferUsageFlags bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    Buffer materialBuffer = uploadBuffer(m_device,graphicsQueue,initCommandBuffer,m_vmaAllocator,bufferUsage,materials.data(),(VkDeviceSize)materials.size());


	std::vector<Image> textureImages;
	for(uint32_t i=0;i<texturePaths.size();i++){
	    int texWidth, texHeight, texChannels;
		stbi_uc* imgData = stbi_load(texturePaths[i].c_str(),&texWidth,&texHeight,&texChannels,4);
		if(imgData == NULL){
		    printf("Failed loading %s file\n",texturePaths[i].c_str());
			stbi_image_free(imgData);
			continue;
		}

		Image newTexture = createImage(m_vmaAllocator,m_device,graphicsQueue,initCommandBuffer,imgData,
		    VkExtent3D(texWidth,texHeight,texChannels),VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT, true);

		textureImages.push_back(newTexture);
		stbi_image_free(imgData);
	}

	for(const auto& img : textureImages){
	    deletionQueue.push_back([&]{
			destroyImage(m_device, m_vmaAllocator,img);
		});
	}

	deletionQueue.push_back([&](){
	    destroyBuffer(m_vmaAllocator,pathSegmentBuffer);
        vkDestroyCommandPool(m_device, initCommandPool, nullptr);
	});

	VkSamplerCreateInfo samplerInfo{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	samplerInfo.compareEnable = VK_FALSE;

	VkSampler linearSampler;
	VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &linearSampler));

	deletionQueue.push_back([&](){
        vkDestroySampler(m_device, linearSampler, nullptr);
	});

    int frameIndex = 0;
    while(!glfwWindowShouldClose(window)){
        glfwPollEvents();

        SwapchainStatus swapchainStatus = updateSwapchain(swapchain,m_device,window,swapchainBuilder,swapchainFormat);
        if(swapchainStatus == Swapchain_NotReady){
            continue;
        }

        if(swapchainStatus == Swapchain_Resized || !drawImage.image){
            printf("Swapchain resized: %dx%d\n",swapchain.width,swapchain.height);
            destroyImage(m_device,m_vmaAllocator,drawImage);

            drawImage = createImage(m_vmaAllocator, m_device, VkExtent3D(swapchain.width,swapchain.height,1),
                VK_FORMAT_R16G16B16A16_SFLOAT, drawImageUsages, false);
        }

        VK_CHECK(vkWaitForFences(m_device, 1, &renderFences[frameIndex],VK_TRUE, UINT64_MAX));

        uint32_t imageIndex = 0;
        VkResult acquireResult = vkAcquireNextImageKHR(m_device, swapchain.swapchain, UINT64_MAX, availableSemaphore[frameIndex], VK_NULL_HANDLE, &imageIndex);
        if(acquireResult == VK_ERROR_OUT_OF_DATE_KHR){
            swapchain.bad = true;
            continue;
        }

        VK_CHECK(acquireResult);

        VK_CHECK(vkResetFences(m_device, 1, &renderFences[frameIndex]));

        VkCommandBuffer commandBuffer = commandBuffers[frameIndex];
        VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

        VkCommandBufferBeginInfo cmdBufferBeginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo));

        transitionImage(commandBuffer, drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        drawBackground(commandBuffer,drawImage.image);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compPipeline);

        PushConstants pushConstants{};
        pushConstants.screenWidth = swapchain.width;
        pushConstants.screenHeight = swapchain.height;

        uint32_t groupX = (pushConstants.screenWidth+15)/16;
        uint32_t groupY = (pushConstants.screenHeight+15)/16;
        vkCmdDispatch(commandBuffer, groupX,groupY,1);

        transitionImage(commandBuffer,drawImage.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transitionImage(commandBuffer, swapchain.images[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        copyImageToImage(commandBuffer, drawImage.image, swapchain.images[imageIndex], drawExtent, VkExtent2D(swapchain.width,swapchain.height));

        transitionImage(commandBuffer, swapchain.images[imageIndex],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        VkCommandBufferSubmitInfo commandBufferSubmitInfo = createCommandBufferSubmitInfo(commandBuffer);
        VkSemaphoreSubmitInfo waitInfo = createSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, availableSemaphore[frameIndex]);
        VkSemaphoreSubmitInfo signalInfo = createSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, presentSemaphore[imageIndex]);
        VkSubmitInfo2 submitInfo = createDrawSubmitInfo(&commandBufferSubmitInfo, &signalInfo, &waitInfo);

        VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submitInfo, renderFences[frameIndex]));

        VkPresentInfoKHR presentInfo{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain.swapchain;
        presentInfo.pWaitSemaphores = &presentSemaphore[imageIndex];
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pImageIndices = &imageIndex;

        VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));

        frameIndex = (frameIndex+1) % FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(m_device);

    for(auto del = deletionQueue.rbegin();del != deletionQueue.rend();del++){
        (*del)();
    }

    for(int i=0;i<FRAMES_IN_FLIGHT;i++){
        vkDestroyFence(m_device, renderFences[i], nullptr);
        vkDestroySemaphore(m_device, availableSemaphore[i], nullptr);
    }

    for(int i=0;i<swapchain.imageCount;i++)
        vkDestroySemaphore(m_device, presentSemaphore[i], nullptr);

    for(int i=0;i<FRAMES_IN_FLIGHT;i++)
        vkDestroyCommandPool(m_device, commandPools[i], nullptr);

    vkDestroySwapchainKHR(m_device, swapchain.swapchain, nullptr);
    for(VkImageView view : swapchain.imageViews)
        vkDestroyImageView(m_device, view, nullptr);

    vkDestroyDevice(m_device, nullptr);
    vkb::destroy_debug_utils_messenger(instance, debug_callback);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
