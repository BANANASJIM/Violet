#include "ResourceFactory.hpp"
#include "VulkanContext.hpp"
#include "Buffer.hpp"
#include "Texture.hpp"
#include "core/Log.hpp"
#include <cassert>

namespace violet {

BufferResource ResourceFactory::createBuffer(VulkanContext* context, const BufferInfo& info) {
    BufferResource result;
    result.size = info.size;


    vk::BufferCreateInfo bufferCreateInfo;
    bufferCreateInfo.size = info.size;
    bufferCreateInfo.usage = info.usage;
    bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = toVmaUsage(info.memoryUsage);
    allocInfo.flags = getVmaFlags(info.memoryUsage);
#ifdef _DEBUG
    // Store debug name in pUserData for leak tracking
    if (!info.debugName.empty()) {
        allocInfo.pUserData = const_cast<char*>(info.debugName.c_str());
    }
#endif

    VkBuffer vkBuffer;
    VkBufferCreateInfo vkBufferCreateInfo = bufferCreateInfo;

    // Get allocation info to retrieve mapped pointer for CPU-visible memory
    VmaAllocationInfo vmaAllocInfo;
    VkResult vmaResult = vmaCreateBuffer(context->getAllocator(), &vkBufferCreateInfo, &allocInfo,
                                         &vkBuffer, &result.allocation, &vmaAllocInfo);

    if (vmaResult != VK_SUCCESS) {
        violet::Log::critical("Renderer", "Failed to create buffer with VMA: error code {}", static_cast<int>(vmaResult));
        assert(false && "Failed to create buffer with VMA");
    }

    result.buffer = vkBuffer;

    // If the memory was created with MAPPED flag, store the mapped pointer
    if (allocInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
        result.mappedData = vmaAllocInfo.pMappedData;
    }

    if (!info.debugName.empty()) {
        vmaSetAllocationName(context->getAllocator(), result.allocation, info.debugName.c_str());
    }


    return result;
}

ImageResource ResourceFactory::createImage(VulkanContext* context, const ImageInfo& info) {
    ImageResource result;
    result.width = info.width;
    result.height = info.height;
    result.format = info.format;


    vk::ImageCreateInfo imageCreateInfo;
    imageCreateInfo.flags = info.flags;
    imageCreateInfo.imageType = info.imageType;
    imageCreateInfo.extent = vk::Extent3D{info.width, info.height, info.depth};
    imageCreateInfo.mipLevels = info.mipLevels;
    imageCreateInfo.arrayLayers = info.arrayLayers;
    imageCreateInfo.format = info.format;
    imageCreateInfo.tiling = info.tiling;
    imageCreateInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageCreateInfo.usage = info.usage;
    imageCreateInfo.samples = info.samples;
    imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = toVmaUsage(info.memoryUsage);
    allocInfo.flags = getVmaFlags(info.memoryUsage);
#ifdef _DEBUG
    // Store debug name in pUserData for leak tracking
    if (!info.debugName.empty()) {
        allocInfo.pUserData = const_cast<char*>(info.debugName.c_str());
    }
#endif

    VkImage vkImage;
    VkImageCreateInfo vkImageCreateInfo = imageCreateInfo;

    VkResult vmaResult = vmaCreateImage(context->getAllocator(), &vkImageCreateInfo, &allocInfo,
                                        &vkImage, &result.allocation, nullptr);

    if (vmaResult != VK_SUCCESS) {
        violet::Log::critical("Renderer", "Failed to create image with VMA: error code {}", static_cast<int>(vmaResult));
        assert(false && "Failed to create image with VMA");
    }

    result.image = vkImage;

    if (!info.debugName.empty()) {
        vmaSetAllocationName(context->getAllocator(), result.allocation, info.debugName.c_str());
    }


    return result;
}

void ResourceFactory::destroyBuffer(VulkanContext* context, BufferResource& buffer) {
    if (buffer.allocation != VK_NULL_HANDLE) {
        // Note: If buffer was created with VMA_ALLOCATION_CREATE_MAPPED_BIT,
        // the memory is automatically unmapped when the allocation is destroyed.
        // We should NOT call vmaUnmapMemory for such allocations.
        buffer.mappedData = nullptr;
        vmaDestroyBuffer(context->getAllocator(), buffer.buffer, buffer.allocation);
        buffer.buffer = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
        buffer.size = 0;
    }
}

void ResourceFactory::destroyImage(VulkanContext* context, ImageResource& image) {
    if (image.allocation != VK_NULL_HANDLE) {
        vmaDestroyImage(context->getAllocator(), image.image, image.allocation);
        image.image = VK_NULL_HANDLE;
        image.allocation = VK_NULL_HANDLE;
        image.width = 0;
        image.height = 0;
        image.format = vk::Format::eUndefined;
    }
}

void* ResourceFactory::mapBuffer(VulkanContext* context, BufferResource& buffer) {
    if (buffer.mappedData) {
        return buffer.mappedData;
    }

    if (vmaMapMemory(context->getAllocator(), buffer.allocation, &buffer.mappedData) != VK_SUCCESS) {
        violet::Log::critical("Renderer", "Failed to map buffer memory");
        assert(false && "Failed to map buffer memory");
        return nullptr;
    }

    return buffer.mappedData;
}


void ResourceFactory::copyBuffer(VulkanContext* context, BufferResource& src, BufferResource& dst, vk::DeviceSize size) {
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands(context);

    vk::BufferCopy copyRegion;
    copyRegion.size = size;
    commandBuffer.copyBuffer(src.buffer, dst.buffer, copyRegion);

    endSingleTimeCommands(context, commandBuffer);
}

void ResourceFactory::copyBufferToImage(VulkanContext* context, BufferResource& buffer, ImageResource& image,
                                        uint32_t width, uint32_t height) {
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands(context);

    vk::BufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    commandBuffer.copyBufferToImage(buffer.buffer, image.image, vk::ImageLayout::eTransferDstOptimal, region);

    endSingleTimeCommands(context, commandBuffer);
}

vk::ImageView ResourceFactory::createImageView(VulkanContext* context, const ImageResource& image,
                                               vk::ImageViewType viewType, vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = image.image;
    viewInfo.viewType = viewType;
    viewInfo.format = image.format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    return context->getDevice().createImageView(viewInfo);
}

VmaMemoryUsage ResourceFactory::toVmaUsage(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::GPU_ONLY:
        return VMA_MEMORY_USAGE_GPU_ONLY;
    case MemoryUsage::CPU_TO_GPU:
        return VMA_MEMORY_USAGE_CPU_TO_GPU;
    case MemoryUsage::GPU_TO_CPU:
        return VMA_MEMORY_USAGE_GPU_TO_CPU;
    case MemoryUsage::CPU_ONLY:
        return VMA_MEMORY_USAGE_CPU_ONLY;
    default:
        return VMA_MEMORY_USAGE_AUTO;
    }
}

VmaAllocationCreateFlags ResourceFactory::getVmaFlags(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::CPU_TO_GPU:
    case MemoryUsage::GPU_TO_CPU:
    case MemoryUsage::CPU_ONLY:
        return VMA_ALLOCATION_CREATE_MAPPED_BIT;
    default:
        return 0;
    }
}

eastl::unique_ptr<Texture> ResourceFactory::createWhiteTexture(VulkanContext* context) {
    auto texture = eastl::make_unique<Texture>();

    // Create 4x4 white texture
    constexpr uint32_t width = 4;
    constexpr uint32_t height = 4;
    constexpr uint32_t channels = 4;
    eastl::vector<uint8_t> pixels(width * height * channels, 255);

    texture->loadFromMemory(context, pixels.data(), pixels.size(), width, height, channels, false);
    return texture;
}

eastl::unique_ptr<Texture> ResourceFactory::createBlackTexture(VulkanContext* context) {
    auto texture = eastl::make_unique<Texture>();

    // Create 4x4 black texture
    constexpr uint32_t width = 4;
    constexpr uint32_t height = 4;
    constexpr uint32_t channels = 4;
    eastl::vector<uint8_t> pixels(width * height * channels);

    // Fill with black (RGB=0) but alpha=255
    for (uint32_t i = 0; i < width * height; ++i) {
        pixels[i * 4 + 0] = 0;   // R
        pixels[i * 4 + 1] = 0;   // G
        pixels[i * 4 + 2] = 0;   // B
        pixels[i * 4 + 3] = 255; // A
    }

    texture->loadFromMemory(context, pixels.data(), pixels.size(), width, height, channels, false);
    return texture;
}

eastl::unique_ptr<Texture> ResourceFactory::createCubemapTexture(VulkanContext* context, const eastl::array<eastl::string, 6>& facePaths) {
    auto texture = eastl::make_unique<Texture>();
    texture->loadCubemap(context, facePaths);
    return texture;
}

eastl::unique_ptr<Texture> ResourceFactory::createHDRTexture(VulkanContext* context, const eastl::string& hdrPath) {
    auto texture = eastl::make_unique<Texture>();
    texture->loadHDR(context, hdrPath);
    return texture;
}

eastl::unique_ptr<Texture> ResourceFactory::createHDRCubemap(VulkanContext* context, const eastl::string& hdrPath) {
    auto texture = eastl::make_unique<Texture>();
    texture->loadEquirectangularToCubemap(context, hdrPath);
    return texture;
}

} // namespace violet