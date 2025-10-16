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
};

struct TransientBuffer {
    vk::Buffer buffer;
    VmaAllocation allocation;
};

class TransientPool {
public:
    TransientPool() = default;
    ~TransientPool();

    TransientPool(const TransientPool&) = delete;
    TransientPool& operator=(const TransientPool&) = delete;

    void init(VulkanContext* ctx);
    void cleanup();

    TransientImage createImage(const ImageDesc& desc, uint32_t firstUse, uint32_t lastUse);
    TransientBuffer createBuffer(const BufferDesc& desc, uint32_t firstUse, uint32_t lastUse);

    void reset();

private:
    struct AllocationBlock {
        VmaAllocation allocation;
        vk::DeviceSize size;
        uint32_t firstUse;
        uint32_t lastUse;
        bool inUse;
    };

    VulkanContext* context = nullptr;
    VmaAllocator allocator = VK_NULL_HANDLE;
    eastl::vector<TransientImage> images;
    eastl::vector<TransientBuffer> buffers;
    eastl::vector<AllocationBlock> allocationPool;

    VmaAllocation findOrCreateAllocation(vk::DeviceSize size, uint32_t firstUse, uint32_t lastUse);
};

} // namespace violet