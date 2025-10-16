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

VmaAllocation TransientPool::findOrCreateAllocation(vk::DeviceSize size, uint32_t firstUse, uint32_t lastUse) {
    // 查找可复用的 allocation（生命周期不重叠）
    for (auto& block : allocationPool) {
        if (!block.inUse && block.lastUse < firstUse && block.size >= size) {
            block.inUse = true;
            block.firstUse = firstUse;
            block.lastUse = lastUse;
            return block.allocation;
        }
    }

    // 如果没有找到可复用的，返回nullptr，让调用者创建新的
    // 新的allocation会在createImage/createBuffer中通过vmaCreateImage/vmaCreateBuffer创建
    return VK_NULL_HANDLE;
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

    // Calculate size requirement (need a dummy image for this)
    VkImage dummyImage;
    VkResult result = vkCreateImage(context->getDevice(),
        reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), nullptr, &dummyImage);
    if (result != VK_SUCCESS) {
        Log::error("TransientPool", "Failed to create dummy image for size calculation");
        return {};
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(context->getDevice(), dummyImage, &memReqs);
    vkDestroyImage(context->getDevice(), dummyImage, nullptr);

    // Try to find a reusable allocation
    VmaAllocation allocation = findOrCreateAllocation(memReqs.size, firstUse, lastUse);

    VkImage vkImage;
    if (allocation) {
        // Reuse existing allocation with aliasing
        result = vmaCreateAliasingImage(allocator, allocation,
            reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &vkImage);

        if (result != VK_SUCCESS) {
            Log::error("TransientPool", "Failed to create aliasing image");
            return {};
        }
    } else {
        // Create new image with new allocation
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        result = vmaCreateImage(allocator,
            reinterpret_cast<const VkImageCreateInfo*>(&imageInfo),
            &allocInfo,
            &vkImage,
            &allocation,
            nullptr);

        if (result != VK_SUCCESS) {
            Log::error("TransientPool", "Failed to create image with allocation");
            return {};
        }

        // Add this new allocation to the pool for future reuse
        AllocationBlock block;
        block.allocation = allocation;
        block.size = memReqs.size;
        block.firstUse = firstUse;
        block.lastUse = lastUse;
        block.inUse = true;
        allocationPool.push_back(block);
    }

    // Determine aspect mask based on format (depth vs color)
    bool isDepthFormat = (desc.format == vk::Format::eD32Sfloat ||
                         desc.format == vk::Format::eD24UnormS8Uint ||
                         desc.format == vk::Format::eD16Unorm ||
                         desc.format == vk::Format::eD32SfloatS8Uint);

    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = vkImage;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = desc.format;
    viewInfo.subresourceRange.aspectMask = isDepthFormat ?
        vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
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

    // Calculate size requirement (need a dummy buffer for this)
    VkBuffer dummyBuffer;
    VkResult result = vkCreateBuffer(context->getDevice(),
        reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), nullptr, &dummyBuffer);
    if (result != VK_SUCCESS) {
        Log::error("TransientPool", "Failed to create dummy buffer for size calculation");
        return {};
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(context->getDevice(), dummyBuffer, &memReqs);
    vkDestroyBuffer(context->getDevice(), dummyBuffer, nullptr);

    // Try to find a reusable allocation
    VmaAllocation allocation = findOrCreateAllocation(memReqs.size, firstUse, lastUse);

    VkBuffer vkBuffer;
    if (allocation) {
        // Reuse existing allocation with aliasing
        result = vmaCreateAliasingBuffer(allocator, allocation,
            reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &vkBuffer);

        if (result != VK_SUCCESS) {
            Log::error("TransientPool", "Failed to create aliasing buffer");
            return {};
        }
    } else {
        // Create new buffer with new allocation
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        result = vmaCreateBuffer(allocator,
            reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
            &allocInfo,
            &vkBuffer,
            &allocation,
            nullptr);

        if (result != VK_SUCCESS) {
            Log::error("TransientPool", "Failed to create buffer with allocation");
            return {};
        }

        // Add this new allocation to the pool for future reuse
        AllocationBlock block;
        block.allocation = allocation;
        block.size = memReqs.size;
        block.firstUse = firstUse;
        block.lastUse = lastUse;
        block.inUse = true;
        allocationPool.push_back(block);
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
