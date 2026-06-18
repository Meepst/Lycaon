#pragma once
#include "common.h"
#include "resources.h"

bool createDDSImage(Image& image, VkDevice device, VmaAllocator allocator, VkCommandPool commandPool,
    VkCommandBuffer commandBuffer, VkQueue queue,  const Buffer& staging, const char* path, bool isSRGB);
