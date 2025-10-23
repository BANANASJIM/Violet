#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/array.h>

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
    vk::ImageCreateFlags flags = {};
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
    vk::ImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    vk::Format format = vk::Format::eUndefined;
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;  // Current layout for RenderGraph import
};

class ResourceFactory {
public:
    static BufferResource createBuffer(VulkanContext* context, const BufferInfo& info);
    static ImageResource createImage(VulkanContext* context, const ImageInfo& info);

    static void destroyBuffer(VulkanContext* context, BufferResource& buffer);
    static void destroyImage(VulkanContext* context, ImageResource& image);

    static void* mapBuffer(VulkanContext* context, BufferResource& buffer);

    // Immediate transfer methods (uses single-time command buffers)
    static void copyBuffer(VulkanContext* context, BufferResource& src, BufferResource& dst, vk::DeviceSize size);
    static void copyBufferToImage(VulkanContext* context, BufferResource& buffer, ImageResource& image,
                                  uint32_t width, uint32_t height);

    // Async transfer methods (for use within command buffers)
    // These methods can be batched in RenderGraph TransferPass for better performance
    static void copyBufferAsync(VulkanContext* context, vk::CommandBuffer cmd,
                               BufferResource& src, BufferResource& dst, vk::DeviceSize size);
    static void copyBufferToImageAsync(VulkanContext* context, vk::CommandBuffer cmd,
                                      BufferResource& buffer, ImageResource& image,
                                      uint32_t width, uint32_t height);
    static void transitionImageLayout(VulkanContext* context, vk::CommandBuffer cmd,
                                     ImageResource& image, vk::Format format,
                                     vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                     uint32_t arrayLayers = 1);

    static vk::ImageView createImageView(VulkanContext* context, const ImageResource& image,
                                         vk::ImageViewType viewType = vk::ImageViewType::e2D,
                                         vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor);

    // High-level texture creation methods
    static eastl::unique_ptr<Texture> createWhiteTexture(VulkanContext* context);
    static eastl::unique_ptr<Texture> createBlackTexture(VulkanContext* context);
    static eastl::unique_ptr<Texture> createCubemapTexture(VulkanContext* context, const eastl::array<eastl::string, 6>& facePaths);

    // HDR texture support
    static eastl::unique_ptr<Texture> createHDRTexture(VulkanContext* context, const eastl::string& hdrPath);
    static eastl::unique_ptr<Texture> createHDRCubemap(VulkanContext* context, const eastl::string& hdrPath);

    // Single-time command execution with functional interface
    template<typename Func>
    static void executeSingleTimeCommands(VulkanContext* context, Func&& func) {
        vk::CommandBuffer cmd = beginSingleTimeCommands(context);
        func(cmd);
        endSingleTimeCommands(context, cmd);
    }

private:
    static VmaMemoryUsage toVmaUsage(MemoryUsage usage);
    static VmaAllocationCreateFlags getVmaFlags(MemoryUsage usage);

    // Internal helpers for single-time commands
    static vk::CommandBuffer beginSingleTimeCommands(VulkanContext* context);
    static void endSingleTimeCommands(VulkanContext* context, vk::CommandBuffer commandBuffer);
    static void endSingleTimeCommands(VulkanContext* context, const vk::raii::CommandBuffer& commandBuffer);
};

} // namespace violet