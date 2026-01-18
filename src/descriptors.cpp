#include "descriptors.h"

void DescriptorLayoutBuilder::addBinding(uint32_t binding, VkDescriptorType type){
    VkDescriptorSetLayoutBinding newbinding{};
    newbinding.binding = binding;
    newbinding.descriptorCount = 1;
    newbinding.descriptorType = type;

    bindings.push_back(newbinding);
}

void DescriptorLayoutBuilder::clear(){
    bindings.clear();
}

VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags){
    for(auto& bind : bindings){
        bind.stageFlags |= shaderStages;
    }

    VkDescriptorSetLayoutCreateInfo createInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    createInfo.pNext = pNext;

    createInfo.pBindings = bindings.data();
    createInfo.bindingCount = (uint32_t)bindings.size();
    createInfo.flags = flags;

    VkDescriptorSetLayout layout = 0;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &layout));

    return layout;
}


void DescriptorAllocator::initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios){
    ratios.clear();

    for(auto r : poolRatios){
        ratios.push_back(r);
    }
    VkDescriptorPool newPool = createPool(device, maxSets, poolRatios);

    setsPerPool = maxSets*1.5;
    readyPools.push_back(newPool);
}

VkDescriptorPool DescriptorAllocator::createPool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios){
    std::vector<VkDescriptorPoolSize> poolSizes;
    for(PoolSizeRatio ratio : poolRatios){
        poolSizes.push_back(VkDescriptorPoolSize{
           .type = ratio.type,
           .descriptorCount = uint32_t(ratio.ratio*setCount)
        });
    }

    VkDescriptorPoolCreateInfo createInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    createInfo.flags = 0;
    createInfo.maxSets = setCount;
    createInfo.poolSizeCount = (uint32_t)poolSizes.size();
    createInfo.pPoolSizes = poolSizes.data();

    VkDescriptorPool newPool;
    VK_CHECK(vkCreateDescriptorPool(device, &createInfo, nullptr, &newPool));
    return newPool;
}

VkDescriptorPool DescriptorAllocator::getPool(VkDevice device){
    VkDescriptorPool newPool;
    if(readyPools.size() != 0){
        newPool = readyPools.back();
        readyPools.pop_back();
    }else{
        newPool = createPool(device, setsPerPool, ratios);

        setsPerPool = setsPerPool*1.5;
        if(setsPerPool > 4092){
            setsPerPool = 4092;
        }
    }

    return newPool;
}

void DescriptorAllocator::clearPools(VkDevice device){
    for(auto p : readyPools)
        vkResetDescriptorPool(device,p,0);
    for(auto p : fullPools){
        vkResetDescriptorPool(device, p, 0);
        readyPools.push_back(p);
    }
    fullPools.clear();
}

void DescriptorAllocator::destroyPools(VkDevice device){
    for(auto p : readyPools)
        vkDestroyDescriptorPool(device, p, nullptr);
    readyPools.clear();
    for(auto p : fullPools){
        vkDestroyDescriptorPool(device, p, nullptr);
    }
    fullPools.clear();
}

VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout){
    VkDescriptorPool poolToUse = getPool(device);

    VkDescriptorSetAllocateInfo allocInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = poolToUse;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descSet;
    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &descSet);

    if(result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL){
        fullPools.push_back(poolToUse);

        poolToUse = getPool(device);
        allocInfo.descriptorPool = poolToUse;

        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &descSet));
    }

    readyPools.push_back(poolToUse);
    return descSet;
}

void DescriptorWriter::writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type){
    VkDescriptorBufferInfo &info = bufferInfos.emplace_back(VkDescriptorBufferInfo{
       .buffer = buffer,
       .offset = offset,
       .range = size
    });

    VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstBinding = binding;
    write.dstSet = VK_NULL_HANDLE;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &info;

    writes.push_back(write);
}

void DescriptorWriter::writeImage(int binding, VkImageView imageView, VkSampler sampler, VkImageLayout layout, VkDescriptorType type){
    VkDescriptorImageInfo &info = imageInfos.emplace_back(VkDescriptorImageInfo{
        .sampler = sampler,
        .imageView = imageView,
        .imageLayout = layout
    });

    VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstBinding = binding;
    write.dstSet = VK_NULL_HANDLE;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pImageInfo = &info;

    writes.push_back(write);
}

void DescriptorWriter::clear(){
    imageInfos.clear();
    writes.clear();
    bufferInfos.clear();
}

void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set){
    for(VkWriteDescriptorSet &write : writes){
        write.dstSet = set;
    }

    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(),0,nullptr);
}
