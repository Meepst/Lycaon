#include "descriptors.h"

namespace vkdh{
    static VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    DescriptorSizeInfo queryDescriptorSizes(VkPhysicalDevice physicalDevice)
    {
        VkPhysicalDeviceDescriptorHeapPropertiesEXT heapProps{};
        heapProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &heapProps;
        vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

        DescriptorSizeInfo info;
        info.samplerDescriptorSize      = heapProps.samplerDescriptorSize;
        info.imageDescriptorSize        = heapProps.imageDescriptorSize;
        info.bufferDescriptorSize       = heapProps.bufferDescriptorSize;
        info.samplerDescriptorAlignment = heapProps.samplerDescriptorAlignment;
        info.imageDescriptorAlignment   = heapProps.imageDescriptorAlignment;
        info.bufferDescriptorAlignment  = heapProps.bufferDescriptorAlignment;
        return info;
    }

    DescriptorHeap::~DescriptorHeap()
    {
        destroy();
    }

    DescriptorHeap::DescriptorHeap(DescriptorHeap&& o) noexcept
    {
        *this = std::move(o);
    }

    DescriptorHeap& DescriptorHeap::operator=(DescriptorHeap&& o) noexcept
    {
        if (this != &o) {
            destroy();
            _device          = o._device;
            _buffer          = o._buffer;
            _memory          = o._memory;
            _deviceAddress   = o._deviceAddress;
            _heapSize        = o._heapSize;
            _descriptorStride = o._descriptorStride;
            _mappedPtr       = o._mappedPtr;
            _capacity        = o._capacity;
            _allocatedCount  = o._allocatedCount;
            _reservedRangeOffset = o._reservedRangeOffset;
            _reservedRangeSize   = o._reservedRangeSize;
            _vmaAllocator    = o._vmaAllocator;
            _vmaAllocation   = o._vmaAllocation;

            o._buffer        = VK_NULL_HANDLE;
            o._memory        = VK_NULL_HANDLE;
            o._deviceAddress = 0;
            o._mappedPtr     = nullptr;
            o._vmaAllocation = nullptr;
        }
        return *this;
    }

    bool DescriptorHeap::createBuffer(const HeapConfig& config,
                                      VkBufferUsageFlags extraUsage,
                                      VkDeviceSize descriptorStride)
    {
        _device           = config.device;
        _descriptorStride = descriptorStride;
        _capacity         = config.maxDescriptors;
        _allocatedCount   = 0;
        _reservedRangeOffset = config.reservedRangeOffset;
        _reservedRangeSize   = config.reservedRangeSize;
        _vmaAllocator     = config.vmaAllocator;

        _heapSize = _reservedRangeOffset + _reservedRangeSize
                   + static_cast<VkDeviceSize>(_capacity) * _descriptorStride;

        // --- Create VkBuffer ---
        VkBufferCreateInfo bufferCI{};
        bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCI.size  = _heapSize;
        bufferCI.usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT
                       | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                       | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                       | extraUsage;
        bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(_device, &bufferCI, nullptr, &_buffer) != VK_SUCCESS) {
            return false;
        }

        // --- Allocate memory ---
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(_device, _buffer, &memReqs);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(config.physicalDevice, &memProps);

        // Find a suitable memory type: prefer DEVICE_LOCAL + HOST_VISIBLE
        VkMemoryPropertyFlags desired =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (config.preferDeviceLocal) {
            desired |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        }

        auto findMemoryType = [&](VkMemoryPropertyFlags flags) -> int32_t {
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((memReqs.memoryTypeBits & (1u << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & flags) == flags) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        };

        int32_t memTypeIndex = findMemoryType(desired);
        if (memTypeIndex < 0 && config.preferDeviceLocal) {
            // Fall back to HOST_VISIBLE only
            desired &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            memTypeIndex = findMemoryType(desired);
        }
        if (memTypeIndex < 0) {
            vkDestroyBuffer(_device, _buffer, nullptr);
            _buffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateFlagsInfo allocFlags{};
        allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext           = &allocFlags;
        allocInfo.allocationSize  = memReqs.size;
        allocInfo.memoryTypeIndex = static_cast<uint32_t>(memTypeIndex);

        if (vkAllocateMemory(_device, &allocInfo, nullptr, &_memory) != VK_SUCCESS) {
            vkDestroyBuffer(_device, _buffer, nullptr);
            _buffer = VK_NULL_HANDLE;
            return false;
        }

        vkBindBufferMemory(_device, _buffer, _memory, 0);

        // Map persistently
        vkMapMemory(_device, _memory, 0, VK_WHOLE_SIZE, 0, &_mappedPtr);

        // Get device address
        VkBufferDeviceAddressInfo addrInfo{};
        addrInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addrInfo.buffer = _buffer;
        _deviceAddress = vkGetBufferDeviceAddress(_device, &addrInfo);

        // Zero-initialise the heap memory
        if (_mappedPtr) {
            std::memset(_mappedPtr, 0, _heapSize);
        }

        return true;
    }
}
