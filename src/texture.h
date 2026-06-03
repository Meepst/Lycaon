#pragma once
#include "common.h"
#include <ktx.h>
#include <ktxvulkan.h>
#include "resources.h"

Image createKTXImage(VmaAllocator allocator,VkDevice device, VkQueue queue,
    VkCommandBuffer commandBuffer, ktxTexture2* texture, VkImageUsageFlags usage);

Image createDDSImage();
