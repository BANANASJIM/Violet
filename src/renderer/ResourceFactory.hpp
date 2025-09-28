#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>

namespace violet {

class VulkanContext;
class Texture;

enum class MemoryUsage {
    GPU_ONLY,       // Device local memory (textures, vertex/index buffers)
    CPU_TO_GPU,     // Host visible, device local if possible (staging buffers)
    GPU_TO_CPU,     // For reading back from GPU
    CPU_ONLY        // Host memory only
};

struct BufferInfo {
    vk::DeviceSize size;
    vk::BufferUsageFlags usage;
    MemoryUsage memoryUsage;
    eastl::string debugName;
};

struct ImageInfo {
    uint32_t width;
    uint32_t height;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    vk::Format format;
    vk::ImageType imageType = vk::ImageType::e2D;
    vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
    vk::ImageUsageFlags usage;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    MemoryUsage memoryUsage = MemoryUsage::GPU_ONLY;
    eastl::string debugName;
};

struct BufferResource {
    vk::Buffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mappedData = nullptr;
    vk::DeviceSize size = 0;
};

struct ImageResource {
    vk::Image image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    vk::Format format = vk::Format::eUndefined;
};

class ResourceFactory {
public:
    static BufferResource createBuffer(VulkanContext* context, const BufferInfo& info);
    static ImageResource createImage(VulkanContext* context, const ImageInfo& info);

    static void destroyBuffer(VulkanContext* context, BufferResource& buffer);
    static void destroyImage(VulkanContext* context, ImageResource& image);

    static void* mapBuffer(VulkanContext* context, BufferResource& buffer);
    static void unmapBuffer(VulkanContext* context, BufferResource& buffer);

    static void copyBuffer(VulkanContext* context, BufferResource& src, BufferResource& dst, vk::DeviceSize size);
    static void copyBufferToImage(VulkanContext* context, BufferResource& buffer, ImageResource& image,
                                  uint32_t width, uint32_t height);

    static vk::ImageView createImageView(VulkanContext* context, const ImageResource& image,
                                         vk::ImageViewType viewType = vk::ImageViewType::e2D,
                                         vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor);

    // High-level texture creation methods
    static eastl::unique_ptr<Texture> createWhiteTexture(VulkanContext* context);
    static eastl::unique_ptr<Texture> createBlackTexture(VulkanContext* context);

private:
    static VmaMemoryUsage toVmaUsage(MemoryUsage usage);
    static VmaAllocationCreateFlags getVmaFlags(MemoryUsage usage);
};

} // namespace violet