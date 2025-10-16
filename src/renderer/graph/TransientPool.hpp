#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
struct ImageDesc;
struct BufferDesc;

struct TransientImage {
    vk::Image image;
    vk::ImageView view;
    VmaAllocation allocation;
    uint32_t frameIndex = 0;  // Which frame in flight allocated this resource
};

struct TransientBuffer {
    vk::Buffer buffer;
    VmaAllocation allocation;
    uint32_t frameIndex = 0;  // Which frame in flight allocated this resource
};

class TransientPool {
public:
    TransientPool() = default;
    ~TransientPool();

    TransientPool(const TransientPool&) = delete;
    TransientPool& operator=(const TransientPool&) = delete;

    void init(VulkanContext* ctx);
    void cleanup();

    // Begin a new frame: recycle allocations from previous frame with same index
    // Safe: App waits on fence before calling, ensuring GPU finished with old resources
    void beginFrame(uint32_t frameIndex);

    TransientImage createImage(const ImageDesc& desc, uint32_t firstUse, uint32_t lastUse, uint32_t frameIndex);
    TransientBuffer createBuffer(const BufferDesc& desc, uint32_t firstUse, uint32_t lastUse, uint32_t frameIndex);

    void reset();  // Deprecated: cleanup all resources (use cleanup() instead)

private:
    struct AllocationBlock {
        VmaAllocation allocation;
        vk::DeviceSize size;
        uint32_t firstUse;
        uint32_t lastUse;
        uint32_t frameIndex;  // Frame index that last used this allocation (for triple buffering)
        bool inUse;
    };

    VulkanContext* context = nullptr;
    VmaAllocator allocator = VK_NULL_HANDLE;
    eastl::vector<TransientImage> images;
    eastl::vector<TransientBuffer> buffers;
    eastl::vector<AllocationBlock> allocationPool;

    VmaAllocation findOrCreateAllocation(vk::DeviceSize size, uint32_t firstUse, uint32_t lastUse, uint32_t frameIndex);
};

} // namespace violet