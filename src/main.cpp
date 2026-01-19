#include "glm/packing.hpp"
#include <vulkan/vulkan_core.h>
#define VMA_IMPLEMENTATION
#include "common.h"
#include "resources.h"
#include "descriptors.h"
#include "scene.h"
#include <deque>
#include <functional>
#include <stb_image.h>
#include <iostream>

struct DrawPushConstants{
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress indexBuffer;
    uint32_t indexCount;
};

struct Config{
    glm::mat4 invViewProjection;
    glm::uvec2 resolution;
    uint32_t maxDepth;
    uint32_t samplesPerFrame;
};

struct Mesh{
    Buffer vertexBuffer;
    Buffer indexBuffer;
    VkDeviceAddress vertexBufferAddress;
    VkDeviceAddress indexBufferAddress;
};

struct alignas(16) Ray{
    glm::vec3 origin;
    glm::vec3 direction;
    glm::vec3 power;
    uint32_t pixelIndex;
};

struct alignas(16) Camera{
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
};

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

    vkb::PhysicalDeviceSelector selector{vkb_instance};
    vkb::PhysicalDevice physicalDevice_return = selector
        .set_minimum_version(1, 4)
        .set_required_features_12(features12)
        .set_required_features_13(features13)
        .set_surface(surface)
        .select().value();

    vkb::DeviceBuilder deviceBuilder{physicalDevice_return};
    vkb::Device device_return = deviceBuilder.build().value();

    VkDevice device = device_return.device;
    VkPhysicalDevice physicalDevice = physicalDevice_return.physical_device;

    vkb::SwapchainBuilder swapchainBuilder{physicalDevice, device, surface};
    VkFormat swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain swapchain_return = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(SCREEN_WIDTH, SCREEN_HEIGHT)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build().value();


    VkExtent2D swapchainExtent = swapchain_return.extent;
    VkSwapchainKHR swapchain = swapchain_return.swapchain;
    std::vector<VkImage> swapchainImages = swapchain_return.get_images().value();
    std::vector<VkImageView> swapchainImageViews = swapchain_return.get_image_views().value();

    VkQueue graphicsQueue =  device_return.get_queue(vkb::QueueType::graphics).value();
    uint32_t queueIndex = device_return.get_queue_index(vkb::QueueType::graphics).value();

    VkCommandPool commandPools[FRAMES_IN_FLIGHT];
    VkCommandBuffer commandBuffers[FRAMES_IN_FLIGHT];

    for(int i=0;i<FRAMES_IN_FLIGHT;i++){
        commandPools[i] = createCommandPool(device, queueIndex);
        createCommandBuffer(device, commandPools[i], commandBuffers[i]);
    }

    VkFence renderFences[FRAMES_IN_FLIGHT];
    VkSemaphore availableSemaphore[FRAMES_IN_FLIGHT];
    std::vector<VkSemaphore> presentSemaphore(swapchainImages.size());

    for(int i=0;i<FRAMES_IN_FLIGHT;i++){
        renderFences[i] = createFence(device);
        availableSemaphore[i] = createSemaphore(device);
    }

    for(int i=0;i<swapchainImages.size();i++){
        presentSemaphore[i] = createSemaphore(device);
    }

    std::deque<std::function<void()>> deletionQueue;

    VmaAllocator vma_allocator;
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &vma_allocator));

    deletionQueue.push_back([&](){vmaDestroyAllocator(vma_allocator);});

    VkExtent3D drawImageExtent = {SCREEN_WIDTH,SCREEN_HEIGHT,1};
    VkExtent2D drawExtent = {SCREEN_WIDTH, SCREEN_HEIGHT};
    VkImageUsageFlags drawImageUsages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    Image drawImage = createImage(vma_allocator, device, drawImageExtent, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageUsages, false);

    deletionQueue.push_back([&](){
        destroyImage(device, vma_allocator, drawImage);
    });

    DescriptorAllocator globalDescAllocator;

    VkDescriptorSet globalDescriptors;
    VkDescriptorSetLayout globalDescriptorLayout;

    std::vector<DescriptorAllocator::PoolSizeRatio> descSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10}
    };

    globalDescAllocator.initPool(device, 10, descSizes);

    printf("total area: %d\n",drawImageExtent.width*drawImageExtent.height);

    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        builder.addBinding(1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        globalDescriptorLayout = builder.build(device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    globalDescriptors = globalDescAllocator.allocate(device, globalDescriptorLayout);

    DescriptorWriter descWriter;
    descWriter.writeImage(0,drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    descWriter.updateSet(device, globalDescriptors);

    deletionQueue.push_back([&](){
        globalDescAllocator.destroyPools(device);
        vkDestroyDescriptorSetLayout(device, globalDescriptorLayout, nullptr);
    });

    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(Config);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayoutCreateInfo computeLayout{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    computeLayout.pSetLayouts = &globalDescriptorLayout;
    computeLayout.setLayoutCount = 1;
    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;

    VkPipelineLayout rayGenerationPipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &computeLayout, nullptr, &rayGenerationPipelineLayout));

    VkShaderModule rayGenerationShader;
    if(!loadShaderModule("build/Debug/spirv/generate_rays.comp.spv", device, &rayGenerationShader)){
        printf("Error when building ray generation compute shader\n");
    }

    VkPipelineShaderStageCreateInfo stageInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = rayGenerationShader;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo{.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    computePipelineCreateInfo.layout = rayGenerationPipelineLayout;
    computePipelineCreateInfo.stage = stageInfo;

    VkPipeline rayGenerationPipeline;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &rayGenerationPipeline));

    vkDestroyShaderModule(device, rayGenerationShader, nullptr);

    deletionQueue.push_back([&](){
        vkDestroyPipelineLayout(device, rayGenerationPipelineLayout, nullptr);
        vkDestroyPipeline(device, rayGenerationPipeline, nullptr);
    });

	VkCommandPool initCommandPool = createCommandPool(device, queueIndex);
	VkCommandBuffer initCommandBuffer = 0;
	createCommandBuffer(device, initCommandPool, initCommandBuffer);

	constexpr size_t pathSegmentSize = SCREEN_WIDTH*SCREEN_HEIGHT*64;
	Buffer pathSegmentBuffer = createBuffer(vma_allocator, pathSegmentSize, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	descWriter.writeBuffer(1, pathSegmentBuffer.buffer, pathSegmentSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	descWriter.updateSet(device, globalDescriptors);

	deletionQueue.push_back([&](){
	    destroyBuffer(vma_allocator,pathSegmentBuffer);
        vkDestroyCommandPool(device, initCommandPool, nullptr);
	});

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<Material> materials;
	std::vector<std::string> texturePaths;
	const char* scenePath = "assets/barramundiFish/glTF/BarramundiFish.gltf";
	if(!parseScene(scenePath,vertices,indices,materials,texturePaths)){
	    printf("failed to load gltf scene\n");
	}

	for(const auto& tex : texturePaths){
        std::cout<<tex<<std::endl;
	}

    int frameIndex = 0;
    while(!glfwWindowShouldClose(window)){
        glfwPollEvents();

        VK_CHECK(vkWaitForFences(device, 1, &renderFences[frameIndex],VK_TRUE, UINT64_MAX));

        uint32_t imageIndex = 0;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, availableSemaphore[frameIndex], VK_NULL_HANDLE, &imageIndex);

        VK_CHECK(vkResetFences(device, 1, &renderFences[frameIndex]));

        VkCommandBuffer commandBuffer = commandBuffers[frameIndex];
        VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

        VkCommandBufferBeginInfo cmdBufferBeginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo));

        transitionImage(commandBuffer, drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        drawBackground(commandBuffer,drawImage.image);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rayGenerationPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rayGenerationPipelineLayout, 0, 1, &globalDescriptors, 0, nullptr);

        Config pushConfig{};
        pushConfig.invViewProjection = glm::mat4(1.0f);
        pushConfig.resolution = glm::uvec2(SCREEN_WIDTH,SCREEN_HEIGHT);
        pushConfig.maxDepth = 8;
        pushConfig.samplesPerFrame = 1;

        vkCmdPushConstants(commandBuffer, rayGenerationPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Config), &pushConfig);

        uint32_t groupX = (pushConfig.resolution.x+15)/16;
        uint32_t groupY = (pushConfig.resolution.y+15)/16;
        vkCmdDispatch(commandBuffer, groupX,groupY,1);

        transitionImage(commandBuffer,drawImage.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transitionImage(commandBuffer, swapchainImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        copyImageToImage(commandBuffer, drawImage.image, swapchainImages[imageIndex], drawExtent, swapchainExtent);

        transitionImage(commandBuffer, swapchainImages[imageIndex],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        VkCommandBufferSubmitInfo commandBufferSubmitInfo = createCommandBufferSubmitInfo(commandBuffer);
        VkSemaphoreSubmitInfo waitInfo = createSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, availableSemaphore[frameIndex]);
        VkSemaphoreSubmitInfo signalInfo = createSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, presentSemaphore[imageIndex]);
        VkSubmitInfo2 submitInfo = createDrawSubmitInfo(&commandBufferSubmitInfo, &signalInfo, &waitInfo);

        VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submitInfo, renderFences[frameIndex]));

        VkPresentInfoKHR presentInfo{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pWaitSemaphores = &presentSemaphore[imageIndex];
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pImageIndices = &imageIndex;

        VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));

        frameIndex = (frameIndex+1) % FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(device);

    for(auto del = deletionQueue.rbegin();del != deletionQueue.rend();del++){
        (*del)();
    }

    for(int i=0;i<FRAMES_IN_FLIGHT;i++){
        vkDestroyFence(device, renderFences[i], nullptr);
        vkDestroySemaphore(device, availableSemaphore[i], nullptr);
    }

    for(int i=0;i<swapchainImages.size();i++)
        vkDestroySemaphore(device, presentSemaphore[i], nullptr);

    for(int i=0;i<FRAMES_IN_FLIGHT;i++)
        vkDestroyCommandPool(device, commandPools[i], nullptr);

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    for(VkImageView view : swapchainImageViews)
        vkDestroyImageView(device, view, nullptr);

    vkDestroyDevice(device, nullptr);
    vkb::destroy_debug_utils_messenger(instance, debug_callback);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
