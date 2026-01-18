#pragma once

#include <GLFW/glfw3.h>
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"
#include <cstdint>
#include <fstream>
#include <vector>
#include <array>
#include <cassert>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#define DEVICE_COUNT 16
#define FRAMES_IN_FLIGHT 2

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 820

#define MAX_TEXTURES 16384

// 1-4 for realtime 100-1000 for offline
#define RAYS_PER_PIXEL 1

#define VK_CHECK(call) \
	do { \
		VkResult result_ = call; \
		assert(result_ == VK_SUCCESS); \
	} while (0)

#define VK_CHECK_SWAPCHAIN(call) \
	do { \
		VkResult result_ = call; \
		assert(result_ == VK_SUCCESS || result_ == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) \
	} while (0)

#define VK_CHECK_FORCE(call) \
	do \
	{ \
		VkResult result_ = call; \
		if (result_ != VK_SUCCESS) \
		{ \
				fprintf(stderr, "%s:%d: %s failed with error %d\n", __FILE__, __LINE__, #call, result_); \
				abort(); \
		} \
	} while (0)
