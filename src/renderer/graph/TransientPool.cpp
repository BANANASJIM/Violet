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

void TransientPool::beginFrame(uint32_t frameIndex) {
    // Destroy ONLY Image/Buffer handles belonging to this frameIndex
    // Other frames may still be in flight on GPU
    auto imgIt = images.begin();
    while (imgIt != images.end()) {
        if (imgIt->frameIndex == frameIndex) {
            if (imgIt->view) {
                context->getDevice().destroyImageView(imgIt->view);
            }
            if (imgIt->image) {
                vkDestroyImage(context->getDevice(), imgIt->image, nullptr);
            }
            imgIt = images.erase(imgIt);
        } else {
            ++imgIt;
        }
    }

    auto bufIt = buffers.begin();
    while (bufIt != buffers.end()) {
        if (bufIt->frameIndex == frameIndex) {
            if (bufIt->buffer) {
                vkDestroyBuffer(context->getDevice(), bufIt->buffer, nullptr);
            }
            bufIt = buffers.erase(bufIt);
        } else {
            ++bufIt;
        }
    }

    // Reset allocations belonging to this frameIndex for recycling
    // Allocations are NOT freed, only marked as available for reuse
    for (auto& block : allocationPool) {
        if (block.frameIndex == frameIndex) {
            block.inUse = false;
            block.lastUse = 0;
        }
    }
}

VmaAllocation TransientPool::findOrCreateAllocation(vk::DeviceSize size, uint32_t firstUse, uint32_t lastUse, uint32_t frameIndex) {
    // Only reuse allocations from the same frameIndex to avoid cross-frame conflicts
    // beginFrame() has already reset inUse for this frameIndex, so we don't need to check lastUse < firstUse
    for (auto& block : allocationPool) {
        if (!block.inUse &&
            block.frameIndex == frameIndex &&
            block.size >= size) {
            block.inUse = true;
            block.firstUse = firstUse;
            block.lastUse = lastUse;
            return block.allocation;
        }
    }

    // Not found: caller will create new allocation and add to pool
    return VK_NULL_HANDLE;
}

TransientImage TransientPool::createImage(const ImageDesc& desc, uint32_t firstUse, uint32_t lastUse, uint32_t frameIndex) {
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

    // Use conservative alignment for aliasing
    memReqs.alignment = std::max(memReqs.alignment, static_cast<VkDeviceSize>(256));

    // Try to find a reusable allocation (only from same frameIndex)
    VmaAllocation allocation = findOrCreateAllocation(memReqs.size, firstUse, lastUse, frameIndex);

    // If no existing allocation found, create a new one using vmaAllocateMemory
    // Following VMA official aliasing example: use preferredFlags, not usage
    if (!allocation) {
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        // Allocate memory directly (not tied to any specific image)
        result = vmaAllocateMemory(allocator, &memReqs, &allocInfo, &allocation, nullptr);

        if (result != VK_SUCCESS) {
            Log::error("TransientPool", "Failed to allocate memory for image");
            return {};
        }

        // Add this new allocation to the pool for future reuse
        AllocationBlock block;
        block.allocation = allocation;
        block.size = memReqs.size;
        block.firstUse = firstUse;
        block.lastUse = lastUse;
        block.frameIndex = frameIndex;  // Tag with frame index
        block.inUse = true;
        allocationPool.push_back(block);
    }

    // Now create aliasing image (works for both new and reused allocations)
    VkImage vkImage;
    result = vmaCreateAliasingImage(allocator, allocation,
        reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &vkImage);

    if (result != VK_SUCCESS) {
        Log::error("TransientPool", "Failed to create aliasing image");
        return {};
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
    transientImg.frameIndex = frameIndex;  // Tag with frame index

    images.push_back(transientImg);
    return transientImg;
}

TransientBuffer TransientPool::createBuffer(const BufferDesc& desc, uint32_t firstUse, uint32_t lastUse, uint32_t frameIndex) {
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

    // Use conservative alignment for aliasing
    memReqs.alignment = std::max(memReqs.alignment, static_cast<VkDeviceSize>(256));

    // Try to find a reusable allocation (only from same frameIndex)
    VmaAllocation allocation = findOrCreateAllocation(memReqs.size, firstUse, lastUse, frameIndex);

    // If no existing allocation found, create a new one using vmaAllocateMemory
    // Following VMA official aliasing example: use preferredFlags, not usage
    if (!allocation) {
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        // Allocate memory directly (not tied to any specific buffer)
        result = vmaAllocateMemory(allocator, &memReqs, &allocInfo, &allocation, nullptr);

        if (result != VK_SUCCESS) {
            Log::error("TransientPool", "Failed to allocate memory for buffer");
            return {};
        }

        // Add this new allocation to the pool for future reuse
        AllocationBlock block;
        block.allocation = allocation;
        block.size = memReqs.size;
        block.firstUse = firstUse;
        block.lastUse = lastUse;
        block.frameIndex = frameIndex;  // Tag with frame index
        block.inUse = true;
        allocationPool.push_back(block);
    }

    // Now create aliasing buffer (works for both new and reused allocations)
    VkBuffer vkBuffer;
    result = vmaCreateAliasingBuffer(allocator, allocation,
        reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &vkBuffer);

    if (result != VK_SUCCESS) {
        Log::error("TransientPool", "Failed to create aliasing buffer");
        return {};
    }

    TransientBuffer transientBuf;
    transientBuf.buffer = vkBuffer;
    transientBuf.allocation = allocation;
    transientBuf.frameIndex = frameIndex;  // Tag with frame index

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
