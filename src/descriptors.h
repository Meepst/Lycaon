#pragma once
#include "common.h"
#include <deque>

namespace vkdh{
    class DescriptorHeap;
    class ResourceHeap;
    class SamplerHeap;

    struct DescriptorSizeInfo{
        VkDeviceSize samplerDescriptorSize = 0;
        VkDeviceSize imageDescriptorSize = 0;
        VkDeviceSize bufferDescriptorSize = 0;
        VkDeviceSize samplerDescriptorAlignment = 0;
        VkDeviceSize imageDescriptorAlignment = 0;
        VkDeviceSize bufferDescriptorAlignment = 0;
    };

    struct DescriptorHandle{
        uint32_t index = UINT32_MAX;

        bool valid() const{return index != UINT32_MAX;}
        explicit operator bool() const { return valid();}
    };

    struct HeapConfig{
        VkDevice device = VK_NULL_HANDLE;
        VkDevice physicalDevice = VK_NULL_HANDLE;
        void* vmaAllocator = nullptr;
        uint32_t maxDescriptors = 65536;
        VkDeviceSize reservedRangeOffset = 0;
        VkDeviceSize reservedRangeSize =  0;
        bool preferDeviceLocal = true;
        bool requireHostVisible = true;
    };

    DescriptorSizeInfo queryDescriptorSize(VkPhysicalDevice physicalDevice);

    class DescriptorHeap{
        public:
            virtual ~DescriptorHeap();

            DescriptorHeap(const DescriptorHeap&) = delete;
            DescriptorHeap& operator=(const DescriptorHeap&) = delete;
            DescriptorHeap(DescriptorHeap&&) noexcept;
            DescriptorHeap& operator=(DescriptorHeap&&) noexcept;

            VkBuffer buffer() const { return m_buffer; }

        protected:

    };
};
