#include "TransientPool.hpp"
#include "renderer/graph/RenderGraph.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

TransientPool::~TransientPool() {
    cleanup();
}

void TransientPool::init(VulkanContext* ctx) {
    context = ctx;
    allocator = context->getAllocator();
    Log::info("TransientPool", "Initialized");
}

void TransientPool::cleanup() {
    reset();

    for (auto& block : allocationPool) {
        if (block.allocation) {
            vmaFreeMemory(allocator, block.allocation);
        }
    }
    allocationPool.clear();

    allocator = VK_NULL_HANDLE;
    context = nullptr;
}

VmaAllocation TransientPool::findOrCreateAllocation(vk::DeviceSize size, uint32_t memoryTypeBits, uint32_t firstUse, uint32_t lastUse) {
    // 查找可复用的 allocation（生命周期不重叠）
    for (auto& block : allocationPool) {
        if (!block.inUse && block.lastUse < firstUse && block.size >= size) {
            block.inUse = true;
            block.lastUse = lastUse;
            return block.allocation;
        }
    }

    // 创建新 allocation
    VkMemoryRequirements memReqs = {};
    memReqs.size = size;
    memReqs.alignment = 0;
    memReqs.memoryTypeBits = memoryTypeBits;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VmaAllocation allocation;
    VkResult result = vmaAllocateMemory(allocator, &memReqs, &allocInfo, &allocation, nullptr);

    if (result != VK_SUCCESS) {
        Log::error("TransientPool", "Failed to allocate memory");
        return VK_NULL_HANDLE;
    }

    AllocationBlock block;
    block.allocation = allocation;
    block.size = size;
    block.memoryTypeIndex = 0;
    block.lastUse = lastUse;
    block.inUse = true;

    allocationPool.push_back(block);
    return allocation;
}

TransientImage TransientPool::createImage(const ImageDesc& desc, uint32_t firstUse, uint32_t lastUse) {
    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = desc.format;
    imageInfo.extent = desc.extent;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.usage = desc.usage;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;

    VkImage vkImage;
    VkResult result = vkCreateImage(context->getDevice(),
        reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), nullptr, &vkImage);

    if (result != VK_SUCCESS) {
        Log::error("TransientPool", "Failed to create image");
        return {};
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(context->getDevice(), vkImage, &memReqs);

    VmaAllocation allocation = findOrCreateAllocation(memReqs.size, memReqs.memoryTypeBits, firstUse, lastUse);
    if (!allocation) {
        vkDestroyImage(context->getDevice(), vkImage, nullptr);
        return {};
    }

    result = vmaBindImageMemory(allocator, allocation, vkImage);
    if (result != VK_SUCCESS) {
        Log::error("TransientPool", "Failed to bind image memory");
        vkDestroyImage(context->getDevice(), vkImage, nullptr);
        return {};
    }

    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = vkImage;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = desc.format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc.arrayLayers;

    vk::ImageView view = context->getDevice().createImageView(viewInfo);

    TransientImage transientImg;
    transientImg.image = vkImage;
    transientImg.view = view;
    transientImg.allocation = allocation;

    images.push_back(transientImg);
    return transientImg;
}

TransientBuffer TransientPool::createBuffer(const BufferDesc& desc, uint32_t firstUse, uint32_t lastUse) {
    vk::BufferCreateInfo bufferInfo;
    bufferInfo.size = desc.size;
    bufferInfo.usage = desc.usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    VkBuffer vkBuffer;
    VkResult result = vkCreateBuffer(context->getDevice(),
        reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), nullptr, &vkBuffer);

    if (result != VK_SUCCESS) {
        Log::error("TransientPool", "Failed to create buffer");
        return {};
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(context->getDevice(), vkBuffer, &memReqs);

    VmaAllocation allocation = findOrCreateAllocation(memReqs.size, memReqs.memoryTypeBits, firstUse, lastUse);
    if (!allocation) {
        vkDestroyBuffer(context->getDevice(), vkBuffer, nullptr);
        Log::error("TransientPool", "Failed to allocate transientBuffer memory");
        return {};
    }

    result = vmaBindBufferMemory(allocator, allocation, vkBuffer);
    if (result != VK_SUCCESS) {
        Log::error("TransientPool", "Failed to bind buffer memory");
        vkDestroyBuffer(context->getDevice(), vkBuffer, nullptr);
        return {};
    }

    TransientBuffer transientBuf;
    transientBuf.buffer = vkBuffer;
    transientBuf.allocation = allocation;

    buffers.push_back(transientBuf);
    return transientBuf;
}

void TransientPool::reset() {
    for (const auto& img : images) {
        if (img.view) {
            context->getDevice().destroyImageView(img.view);
        }
        if (img.image) {
            vkDestroyImage(context->getDevice(), img.image, nullptr);
        }
    }
    images.clear();

    for (const auto& buf : buffers) {
        if (buf.buffer) {
            vkDestroyBuffer(context->getDevice(), buf.buffer, nullptr);
        }
    }
    buffers.clear();

    for (auto& block : allocationPool) {
        block.inUse = false;
        block.lastUse = 0;
    }
}

} // namespace violet
