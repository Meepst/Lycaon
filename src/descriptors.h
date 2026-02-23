#pragma once
#include "common.h"
#include <mutex>

namespace vkdh {
class DescriptorHeap;
class ResourceHeap;
class SamplerHeap;

struct DescriptorSizeInfo {
  VkDeviceSize samplerDescriptorSize = 0;
  VkDeviceSize imageDescriptorSize = 0;
  VkDeviceSize bufferDescriptorSize = 0;
  VkDeviceSize samplerDescriptorAlignment = 0;
  VkDeviceSize imageDescriptorAlignment = 0;
  VkDeviceSize bufferDescriptorAlignment = 0;
};

struct DescriptorHandle {
  uint32_t index = UINT32_MAX;

  bool valid() const { return index != UINT32_MAX; }
  explicit operator bool() const { return valid(); }
};

struct HeapConfig {
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  void *vmaAllocator = nullptr;
  uint32_t maxDescriptors = 65536;
  VkDeviceSize reservedRangeOffset = 0;
  VkDeviceSize reservedRangeSize = 0;
  bool preferDeviceLocal = true;
  bool requireHostVisible = true;
};

DescriptorSizeInfo queryDescriptorSize(VkPhysicalDevice physicalDevice);

class DescriptorHeap {
public:
  virtual ~DescriptorHeap();

  DescriptorHeap(const DescriptorHeap &) = delete;
  DescriptorHeap &operator=(const DescriptorHeap &) = delete;
  DescriptorHeap(DescriptorHeap &&) noexcept;
  DescriptorHeap &operator=(DescriptorHeap &&) noexcept;

  VkBuffer buffer() const { return _buffer; }
  VkDeviceAddress deviceAddress() const { return _deviceAddress; }
  VkDeviceSize heapSize() const { return _heapSize; }
  uint32_t capacity() const { return _capacity; }
  uint32_t allocatedCount() const { return _allocatedCount; }
  void *mappedPtr() const { return _mappedPtr; }

  DescriptorHandle allocate(uint32_t count = 1);
  void free(DescriptorHandle handle, uint32_t count = 1);
  void reset();

  virtual void bind(VkCommandBuffer commandBuffer) const = 0;

protected:
  DescriptorHeap() = default;
  bool createBuffer(const HeapConfig &config, VkBufferUsageFlags extraUsage,
                    VkDeviceSize descriptorStride);
  void destroy();

  VkDevice _device = VK_NULL_HANDLE;
  VkBuffer _buffer = VK_NULL_HANDLE;
  VkDeviceMemory _memory = VK_NULL_HANDLE;
  VkDeviceAddress _deviceAddress = 0;
  VkDeviceSize _heapSize = 0;
  VkDeviceSize _descriptorStride = 0;
  void *_mappedPtr = nullptr;
  uint32_t _capacity = 0;
  uint32_t _allocatedCount = 0;
  VkDeviceSize _reservedRangeOffset = 0;
  VkDeviceSize _reservedRangeSize = 0;
  void *_vmaAllocator = nullptr;
  void *_vmaAllocation = nullptr;

  mutable std::mutex m_mutex;
};

class ResourceHeap final : public DescriptorHeap {
public:
  ResourceHeap() = default;

  bool init(const HeapConfig &config, const DescriptorSizeInfo &sizes);
  void bind(VkCommandBuffer commandBuffer) const override;

  void writeSampledImage(DescriptorHandle handle,
                         const VkImageViewCreateInfo &imageInfo);
  void writeStorageImage(DescriptorHandle handle,
                         const VkImageViewCreateInfo &imageInfo);
  void writeUniformBuffer(DescriptorHandle handle,
                          VkDeviceAddress bufferAddress, VkDeviceSize range);
  void writeStorageBuffer(DescriptorHandle handle,
                          VkDeviceAddress bufferAddress, VkDeviceSize range);
  void writeAccelerationStructure(DescriptorHandle handle,
                                  VkDeviceAddress asAddress,
                                  VkDeviceSize size = 0);
  void writeAccelerationStructures(DescriptorHandle firstHandle,
                                   std::span<const VkDeviceAddress> addresses,
                                   VkDeviceSize size = 0);

  VkDeviceSize imageDescriptorSize() const { return _imageDescSize; }
  VkDeviceSize bufferDescriptorSize() const { return _bufferDescSize; }

private:
  VkDeviceSize _imageDescSize = 0;
  VkDeviceSize _bufferDescSize = 0;

  void writeResourceDescriptor(DescriptorHandle handle, VkDescriptorType type,
                               const VkResourceDescriptorDataEXT &data);
};

class SamplerHeap final : public DescriptorHeap {
public:
  SamplerHeap() = default;

  bool init(const HeapConfig &config, const DescriptorSizeInfo &sizes);
  void bind(VkCommandBuffer commandBuffer) const override;
  void writeSampler(DescriptorHandle handle,
                    const VkSamplerCreateInfo &samplerInfo);
  void writeSamplers(DescriptorHandle firstHandle,
                     std::span<const VkSamplerCreateInfo &> samplerInfos);
};

class DescriptorHeapSystem {
public:
  struct Config {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    void *vmaAllocator = nullptr;

    uint32_t maxResourceDescriptors = 500'000;
    uint32_t maxSamplerDescriptors = 2048;

    // Implementation-reserved ranges (query from properties).
    VkDeviceSize resourceReservedOffset = 0;
    VkDeviceSize resourceReservedSize = 0;
    VkDeviceSize samplerReservedOffset = 0;
    VkDeviceSize samplerReservedSize = 0;
  };

  bool init(const Config& config);
  void destroy();
  void bind(VkCommandBuffer commandBuffer) const;

  const ResourceHeap& resources() {return _resourceHeap;}
  const SamplerHeap& samplers() {return _samplerHeap;}
  const DescriptorSizeInfo& sizeInfo() {return _sizeInfo;}

private:
    ResourceHeap _resourceHeap;
    SamplerHeap _samplerHeap;
    DescriptorSizeInfo _sizeInfo{};
};
}; // namespace vkdh
