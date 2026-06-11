#pragma once
#include "common.h"
#include <ktx.h>
#include <ktxvulkan.h>
#include "resources.h"

Image createKTXImage(VmaAllocator allocator,VkDevice device, VkQueue queue,
    VkCommandBuffer commandBuffer, ktxTexture2* texture, VkImageUsageFlags usage);

bool createDDSImage(Image& image, VkDevice device, VmaAllocator allocator, VkCommandPool commandPool,
    VkCommandBuffer commandBuffer, VkQueue queue,  std::vector<uint8_t> staging, const char* path);
