#include "swapchain.h"
#include "GLFW/glfw3.h"
#include "VkBootstrap.h"
#include <vulkan/vulkan_core.h>

VkFormat getSwapchainFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface){
    uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,&formatCount,0));
    assert(formatCount>0);

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,&formatCount,formats.data()));

    if (formatCount == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
		return VK_FORMAT_R8G8B8A8_UNORM;

	for (uint32_t i = 0; i < formatCount; ++i)
		if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM || formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
			return formats[i].format;

	return formats[0].format;
}

void createSwapchain(Swapchain& result, VkFormat format, GLFWwindow* window, vkb::SwapchainBuilder swapchainBuilder, VkSwapchainKHR old){
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    vkb::Swapchain swapchain_return = swapchainBuilder
        .set_old_swapchain(old)
        .set_desired_format(VkSurfaceFormatKHR{ .format = format, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build().value();

    result.bad = false;
    result.swapchain = swapchain_return.swapchain;
    result.width = width;
    result.height = height;
    result.imageCount = swapchain_return.image_count;
    result.images = swapchain_return.get_images().value();
    result.imageViews = swapchain_return.get_image_views().value();
}

SwapchainStatus updateSwapchain(Swapchain& result, VkDevice device, GLFWwindow* window,
    vkb::SwapchainBuilder swapchainBuilder, VkFormat format){
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    if(width==0||height==0){
        return Swapchain_NotReady;
    }

    if(result.width == width && result.height == height && !result.bad){
        return Swapchain_Ready;
    }

    Swapchain old = result;
    createSwapchain(result,format,window,swapchainBuilder,old.swapchain);

    VK_CHECK(vkDeviceWaitIdle(device));

    vkDestroySwapchainKHR(device, old.swapchain,nullptr);
    return Swapchain_Resized;
}
