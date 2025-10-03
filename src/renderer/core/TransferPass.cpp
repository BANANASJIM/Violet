#include "renderer/core/TransferPass.hpp"
#include "renderer/core/VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

TransferPass::TransferPass() = default;

TransferPass::~TransferPass() {
    cleanup();
}

void TransferPass::init(VulkanContext* ctx, const TransferPassConfig& cfg) {
    context = ctx;
    config = cfg;

    violet::Log::info("Renderer", "Transfer pass '{}' initialized", config.name.c_str());
}

void TransferPass::cleanup() {
    context = nullptr;
}

void TransferPass::begin(vk::CommandBuffer cmd, uint32_t frameIndex) {
    // Insert pre-barriers for layout transitions and synchronization
    insertPreBarriers(cmd);
}

void TransferPass::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    // Execute all configured transfer operations
    executeBufferCopies(cmd);
    executeImageCopies(cmd);
    executeBufferToImageCopies(cmd);
    executeImageToBufferCopies(cmd);

    // Execute custom callback if provided
    if (config.execute) {
        config.execute(cmd, frameIndex);
    }
}

void TransferPass::end(vk::CommandBuffer cmd) {
    // Insert post-barriers for layout transitions back
    insertPostBarriers(cmd);
}

void TransferPass::insertPreBarriers(vk::CommandBuffer cmd) {
    // Image barriers
    if (!config.preImageBarriers.empty()) {
        eastl::vector<vk::ImageMemoryBarrier> imageBarriers;
        imageBarriers.reserve(config.preImageBarriers.size());

        vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eTransfer;

        for (const auto& barrier : config.preImageBarriers) {
            vk::ImageMemoryBarrier imgBarrier;
            imgBarrier.srcAccessMask = barrier.srcAccess;
            imgBarrier.dstAccessMask = barrier.dstAccess;
            imgBarrier.oldLayout = barrier.oldLayout;
            imgBarrier.newLayout = barrier.newLayout;
            imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBarrier.image = barrier.image;
            imgBarrier.subresourceRange = barrier.subresourceRange;

            imageBarriers.push_back(imgBarrier);

            // Accumulate stage flags
            srcStage |= barrier.srcStage;
            dstStage |= barrier.dstStage;
        }

        cmd.pipelineBarrier(
            srcStage,
            dstStage,
            {},
            0, nullptr,
            0, nullptr,
            static_cast<uint32_t>(imageBarriers.size()),
            imageBarriers.data()
        );
    }

    // Buffer barriers
    if (!config.preBufferBarriers.empty()) {
        eastl::vector<vk::BufferMemoryBarrier> bufferBarriers;
        bufferBarriers.reserve(config.preBufferBarriers.size());

        vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eTransfer;

        for (const auto& barrier : config.preBufferBarriers) {
            vk::BufferMemoryBarrier bufBarrier;
            bufBarrier.srcAccessMask = barrier.srcAccess;
            bufBarrier.dstAccessMask = barrier.dstAccess;
            bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufBarrier.buffer = barrier.buffer;
            bufBarrier.offset = barrier.offset;
            bufBarrier.size = barrier.size;

            bufferBarriers.push_back(bufBarrier);

            // Accumulate stage flags
            srcStage |= barrier.srcStage;
            dstStage |= barrier.dstStage;
        }

        cmd.pipelineBarrier(
            srcStage,
            dstStage,
            {},
            0, nullptr,
            static_cast<uint32_t>(bufferBarriers.size()),
            bufferBarriers.data(),
            0, nullptr
        );
    }
}

void TransferPass::insertPostBarriers(vk::CommandBuffer cmd) {
    // Image barriers
    if (!config.postImageBarriers.empty()) {
        eastl::vector<vk::ImageMemoryBarrier> imageBarriers;
        imageBarriers.reserve(config.postImageBarriers.size());

        vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTransfer;
        vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eBottomOfPipe;

        for (const auto& barrier : config.postImageBarriers) {
            vk::ImageMemoryBarrier imgBarrier;
            imgBarrier.srcAccessMask = barrier.srcAccess;
            imgBarrier.dstAccessMask = barrier.dstAccess;
            imgBarrier.oldLayout = barrier.oldLayout;
            imgBarrier.newLayout = barrier.newLayout;
            imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBarrier.image = barrier.image;
            imgBarrier.subresourceRange = barrier.subresourceRange;

            imageBarriers.push_back(imgBarrier);

            // Accumulate stage flags
            srcStage |= barrier.srcStage;
            dstStage |= barrier.dstStage;
        }

        cmd.pipelineBarrier(
            srcStage,
            dstStage,
            {},
            0, nullptr,
            0, nullptr,
            static_cast<uint32_t>(imageBarriers.size()),
            imageBarriers.data()
        );
    }

    // Buffer barriers
    if (!config.postBufferBarriers.empty()) {
        eastl::vector<vk::BufferMemoryBarrier> bufferBarriers;
        bufferBarriers.reserve(config.postBufferBarriers.size());

        vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTransfer;
        vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eBottomOfPipe;

        for (const auto& barrier : config.postBufferBarriers) {
            vk::BufferMemoryBarrier bufBarrier;
            bufBarrier.srcAccessMask = barrier.srcAccess;
            bufBarrier.dstAccessMask = barrier.dstAccess;
            bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufBarrier.buffer = barrier.buffer;
            bufBarrier.offset = barrier.offset;
            bufBarrier.size = barrier.size;

            bufferBarriers.push_back(bufBarrier);

            // Accumulate stage flags
            srcStage |= barrier.srcStage;
            dstStage |= barrier.dstStage;
        }

        cmd.pipelineBarrier(
            srcStage,
            dstStage,
            {},
            0, nullptr,
            static_cast<uint32_t>(bufferBarriers.size()),
            bufferBarriers.data(),
            0, nullptr
        );
    }
}

void TransferPass::executeBufferCopies(vk::CommandBuffer cmd) {
    for (const auto& copy : config.bufferCopies) {
        vk::BufferCopy region;
        region.srcOffset = copy.srcOffset;
        region.dstOffset = copy.dstOffset;
        region.size = copy.size;

        cmd.copyBuffer(copy.srcBuffer, copy.dstBuffer, 1, &region);
    }
}

void TransferPass::executeImageCopies(vk::CommandBuffer cmd) {
    for (const auto& copy : config.imageCopies) {
        cmd.copyImage(
            copy.srcImage, copy.srcLayout,
            copy.dstImage, copy.dstLayout,
            1, &copy.copyRegion
        );
    }
}

void TransferPass::executeBufferToImageCopies(vk::CommandBuffer cmd) {
    for (const auto& copy : config.bufferToImageCopies) {
        cmd.copyBufferToImage(
            copy.srcBuffer,
            copy.dstImage,
            copy.dstLayout,
            1, &copy.copyRegion
        );
    }
}

void TransferPass::executeImageToBufferCopies(vk::CommandBuffer cmd) {
    for (const auto& copy : config.imageToBufferCopies) {
        cmd.copyImageToBuffer(
            copy.srcImage,
            copy.srcLayout,
            copy.dstBuffer,
            1, &copy.copyRegion
        );
    }
}

} // namespace violet
