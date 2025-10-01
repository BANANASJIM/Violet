#include "ResourceLoader.hpp"
#include "VulkanContext.hpp"
#include "Texture.hpp"
#include "Buffer.hpp"
#include "core/Log.hpp"

namespace violet {

ResourceLoader::~ResourceLoader() {
    if (hasPendingTransfers()) {
        violet::Log::warn("Renderer", "ResourceLoader destroyed with pending transfers");
    }
}

void ResourceLoader::queueTextureLoad(const TextureLoadRequest& request) {
    pendingTextures.push_back(request);
}

void ResourceLoader::queueBufferUpload(const BufferUploadRequest& request) {
    pendingBuffers.push_back(request);
}

void ResourceLoader::queueImageUpload(const ImageUploadRequest& request) {
    pendingImages.push_back(request);
}

void ResourceLoader::flush(VulkanContext* context) {
    if (!hasPendingTransfers()) {
        return;
    }

    violet::Log::info("Renderer", "Flushing resource loader: {} buffer uploads, {} image uploads",
                      pendingBuffers.size(), pendingImages.size());

    // Create TransferPass config
    TransferPassConfig transferConfig;
    transferConfig.name = "ResourceLoader Batch Transfer";
    transferConfig.type = PassType::Transfer;

    // Add buffer copies
    for (const auto& bufUpload : pendingBuffers) {
        BufferCopyRegion region;
        region.srcBuffer = bufUpload.stagingBuffer.buffer;
        region.dstBuffer = bufUpload.targetBuffer.buffer;
        region.srcOffset = bufUpload.srcOffset;
        region.dstOffset = bufUpload.dstOffset;
        region.size = bufUpload.size;
        transferConfig.bufferCopies.push_back(region);
    }

    // Add image uploads
    for (const auto& imgUpload : pendingImages) {
        // Pre-barrier: transition to transfer dst
        if (imgUpload.needsPreBarrier) {
            ImageBarrier preBarrier;
            preBarrier.image = imgUpload.targetImage.image;
            preBarrier.oldLayout = imgUpload.initialLayout;
            preBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            preBarrier.subresourceRange = vk::ImageSubresourceRange{
                vk::ImageAspectFlagBits::eColor,
                0, 1,  // mip levels
                0, imgUpload.arrayLayers
            };
            preBarrier.srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
            preBarrier.dstStage = vk::PipelineStageFlagBits::eTransfer;
            preBarrier.srcAccess = {};
            preBarrier.dstAccess = vk::AccessFlagBits::eTransferWrite;
            transferConfig.preImageBarriers.push_back(preBarrier);
        }

        // Buffer-to-image copy
        if (imgUpload.arrayLayers == 1) {
            // Single image
            BufferImageCopyRegion copyRegion;
            copyRegion.srcBuffer = imgUpload.stagingBuffer.buffer;
            copyRegion.dstImage = imgUpload.targetImage.image;
            copyRegion.dstLayout = vk::ImageLayout::eTransferDstOptimal;

            vk::BufferImageCopy copy;
            copy.bufferOffset = 0;
            copy.bufferRowLength = 0;
            copy.bufferImageHeight = 0;
            copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = vk::Offset3D{0, 0, 0};
            copy.imageExtent = vk::Extent3D{imgUpload.width, imgUpload.height, 1};
            copyRegion.copyRegion = copy;

            transferConfig.bufferToImageCopies.push_back(copyRegion);
        } else {
            // Cubemap or array texture - copy each layer separately
            uint32_t layerSize = imgUpload.width * imgUpload.height * 4;  // Assuming RGBA
            for (uint32_t layer = 0; layer < imgUpload.arrayLayers; ++layer) {
                BufferImageCopyRegion copyRegion;
                copyRegion.srcBuffer = imgUpload.stagingBuffer.buffer;
                copyRegion.dstImage = imgUpload.targetImage.image;
                copyRegion.dstLayout = vk::ImageLayout::eTransferDstOptimal;

                vk::BufferImageCopy copy;
                copy.bufferOffset = layer * layerSize;
                copy.bufferRowLength = 0;
                copy.bufferImageHeight = 0;
                copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                copy.imageSubresource.mipLevel = 0;
                copy.imageSubresource.baseArrayLayer = layer;
                copy.imageSubresource.layerCount = 1;
                copy.imageOffset = vk::Offset3D{0, 0, 0};
                copy.imageExtent = vk::Extent3D{imgUpload.width, imgUpload.height, 1};
                copyRegion.copyRegion = copy;

                transferConfig.bufferToImageCopies.push_back(copyRegion);
            }
        }

        // Post-barrier: transition to final layout
        if (imgUpload.needsPostBarrier) {
            ImageBarrier postBarrier;
            postBarrier.image = imgUpload.targetImage.image;
            postBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            postBarrier.newLayout = imgUpload.finalLayout;
            postBarrier.subresourceRange = vk::ImageSubresourceRange{
                vk::ImageAspectFlagBits::eColor,
                0, 1,  // mip levels
                0, imgUpload.arrayLayers
            };
            postBarrier.srcStage = vk::PipelineStageFlagBits::eTransfer;
            postBarrier.dstStage = vk::PipelineStageFlagBits::eFragmentShader;
            postBarrier.srcAccess = vk::AccessFlagBits::eTransferWrite;
            postBarrier.dstAccess = vk::AccessFlagBits::eShaderRead;
            transferConfig.postImageBarriers.push_back(postBarrier);
        }
    }

    // Create and execute transfer pass
    auto transferPass = eastl::make_unique<TransferPass>();
    transferPass->init(context, transferConfig);

    // Execute using single-time command buffer
    ResourceFactory::executeSingleTimeCommands(context, [&](vk::CommandBuffer cmd) {
        transferPass->begin(cmd, 0);
        transferPass->execute(cmd, 0);
        transferPass->end(cmd);
    });

    // Cleanup staging buffers
    for (auto& bufUpload : pendingBuffers) {
        ResourceFactory::destroyBuffer(context, bufUpload.stagingBuffer);
    }
    for (auto& imgUpload : pendingImages) {
        ResourceFactory::destroyBuffer(context, imgUpload.stagingBuffer);
    }

    // Clear requests
    clear();

    violet::Log::info("Renderer", "Resource loader flush complete");
}

void ResourceLoader::flushAndWait(VulkanContext* context) {
    flush(context);
    // Note: flush() already waits via endSingleTimeCommands
}

void ResourceLoader::clear() {
    pendingTextures.clear();
    pendingBuffers.clear();
    pendingImages.clear();
}

bool ResourceLoader::hasPendingTransfers() const {
    return !pendingTextures.empty() || !pendingBuffers.empty() || !pendingImages.empty();
}

} // namespace violet
