#pragma once
#include "common.h"

struct Swapchain{
  VkSwapchainKHR swapchain;
  std::vector<VkImage> images;
  std::vector<VkImageView> imageViews;
  uint32_t width, height;
  uint32_t imageCount;
  VkPresentModeKHR presentMode;
  bool bad;
};

enum SwapchainStatus{
    Swapchain_Ready,
    Swapchain_Resized,
    Swapchain_NotReady
};

VkFormat getSwapchainFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
void createSwapchain(Swapchain& result, VkFormat format, GLFWwindow* window,
    vkb::SwapchainBuilder swapchainBuilder, VkSwapchainKHR old,
    VkPresentModeKHR presentMode);
SwapchainStatus updateSwapchain(Swapchain& result, VkDevice device, GLFWwindow* window,
    vkb::SwapchainBuilder swapchainBuilder, VkFormat format, VkPresentModeKHR presentMode);
VkPresentModeKHR pickPresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool vsyncEnabled);
