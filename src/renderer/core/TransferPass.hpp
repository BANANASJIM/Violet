#pragma once

#include "renderer/core/Pass.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/functional.h>

namespace violet {

class VulkanContext;

// Transfer operation types
enum class TransferOpType {
    BufferToBuffer,   // Copy between buffers
    ImageToImage,     // Copy between images
    BufferToImage,    // Upload buffer data to image
    ImageToBuffer     // Read image data to buffer
};

// Buffer copy region
struct BufferCopyRegion {
    vk::Buffer srcBuffer = VK_NULL_HANDLE;
    vk::Buffer dstBuffer = VK_NULL_HANDLE;
    vk::DeviceSize srcOffset = 0;
    vk::DeviceSize dstOffset = 0;
    vk::DeviceSize size = 0;
};

// Image copy region
struct ImageCopyRegion {
    vk::Image srcImage = VK_NULL_HANDLE;
    vk::Image dstImage = VK_NULL_HANDLE;
    vk::ImageLayout srcLayout = vk::ImageLayout::eTransferSrcOptimal;
    vk::ImageLayout dstLayout = vk::ImageLayout::eTransferDstOptimal;
    vk::ImageCopy copyRegion;
};

// Buffer-to-Image copy region
struct BufferImageCopyRegion {
    vk::Buffer srcBuffer = VK_NULL_HANDLE;
    vk::Image dstImage = VK_NULL_HANDLE;
    vk::ImageLayout dstLayout = vk::ImageLayout::eTransferDstOptimal;
    vk::BufferImageCopy copyRegion;
};

// Image-to-Buffer copy region
struct ImageBufferCopyRegion {
    vk::Image srcImage = VK_NULL_HANDLE;
    vk::Buffer dstBuffer = VK_NULL_HANDLE;
    vk::ImageLayout srcLayout = vk::ImageLayout::eTransferSrcOptimal;
    vk::BufferImageCopy copyRegion;
};

// Image barrier for layout transitions
struct ImageBarrier {
    vk::Image image = VK_NULL_HANDLE;
    vk::ImageLayout oldLayout = vk::ImageLayout::eUndefined;
    vk::ImageLayout newLayout = vk::ImageLayout::eTransferDstOptimal;
    vk::ImageSubresourceRange subresourceRange{
        vk::ImageAspectFlagBits::eColor,  // aspectMask
        0,  // baseMipLevel
        1,  // levelCount
        0,  // baseArrayLayer
        1   // layerCount
    };
    vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
    vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eTransfer;
    vk::AccessFlags srcAccess = {};
    vk::AccessFlags dstAccess = vk::AccessFlagBits::eTransferWrite;
};

// Buffer barrier for memory synchronization
struct BufferBarrier {
    vk::Buffer buffer = VK_NULL_HANDLE;
    vk::DeviceSize offset = 0;
    vk::DeviceSize size = VK_WHOLE_SIZE;
    vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
    vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eTransfer;
    vk::AccessFlags srcAccess = {};
    vk::AccessFlags dstAccess = vk::AccessFlagBits::eTransferWrite;
};

// Transfer pass configuration
struct TransferPassConfig : public PassConfigBase {
    // Transfer operations
    eastl::vector<BufferCopyRegion> bufferCopies;
    eastl::vector<ImageCopyRegion> imageCopies;
    eastl::vector<BufferImageCopyRegion> bufferToImageCopies;
    eastl::vector<ImageBufferCopyRegion> imageToBufferCopies;

    // Barriers for layout transitions and synchronization
    eastl::vector<ImageBarrier> preImageBarriers;
    eastl::vector<ImageBarrier> postImageBarriers;
    eastl::vector<BufferBarrier> preBufferBarriers;
    eastl::vector<BufferBarrier> postBufferBarriers;

    // Default constructor
    TransferPassConfig() {
        type = PassType::Transfer;
        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
        srcAccess = vk::AccessFlagBits::eTransferWrite;
        dstAccess = vk::AccessFlagBits::eTransferRead;
    }
};

// Transfer pass implementation for GPU resource transfers
class TransferPass : public Pass {
public:
    TransferPass();
    ~TransferPass() override;

    // Initialization
    void init(VulkanContext* context, const TransferPassConfig& config);
    void cleanup() override;

    // Pass interface implementation
    void begin(vk::CommandBuffer cmd, uint32_t frameIndex) override;
    void execute(vk::CommandBuffer cmd, uint32_t frameIndex) override;
    void end(vk::CommandBuffer cmd) override;

    PassType getType() const override { return PassType::Transfer; }
    const eastl::string& getName() const override { return config.name; }

    // Barrier configuration access
    vk::PipelineStageFlags getSrcStage() const override { return config.srcStage; }
    vk::PipelineStageFlags getDstStage() const override { return config.dstStage; }
    vk::AccessFlags getSrcAccess() const override { return config.srcAccess; }
    vk::AccessFlags getDstAccess() const override { return config.dstAccess; }

    // Transfer-specific API
    const TransferPassConfig& getConfig() const { return config; }

private:
    void insertPreBarriers(vk::CommandBuffer cmd);
    void insertPostBarriers(vk::CommandBuffer cmd);
    void executeBufferCopies(vk::CommandBuffer cmd);
    void executeImageCopies(vk::CommandBuffer cmd);
    void executeBufferToImageCopies(vk::CommandBuffer cmd);
    void executeImageToBufferCopies(vk::CommandBuffer cmd);

private:
    VulkanContext* context = nullptr;
    TransferPassConfig config;
};

} // namespace violet
