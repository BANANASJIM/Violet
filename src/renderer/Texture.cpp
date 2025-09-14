#include "Texture.hpp"
#include "VulkanContext.hpp"
#include "Buffer.hpp"
#include <ktx.h>
#include <stb_image.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace violet {

void Texture::loadFromFile(VulkanContext* ctx, const eastl::string& filePath) {
    context = ctx;

    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    vk::DeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) {
        throw std::runtime_error("Failed to load texture image!");
    }

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;
    createBuffer(ctx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* data = ctx->getDevice().mapMemory(stagingBufferMemory, 0, imageSize);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    ctx->getDevice().unmapMemory(stagingBufferMemory);

    stbi_image_free(pixels);

    createImage(ctx, texWidth, texHeight, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);

    transitionImageLayout(ctx, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    copyBufferToImage(ctx, stagingBuffer, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    transitionImageLayout(ctx, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    ctx->getDevice().destroyBuffer(stagingBuffer);
    ctx->getDevice().freeMemory(stagingBufferMemory);

    createImageView(ctx, vk::Format::eR8G8B8A8Srgb);
    createSampler(ctx);
}

void Texture::loadFromKTX2(VulkanContext* ctx, const eastl::string& filePath) {
    context = ctx;

    ktxTexture2* kTexture;
    KTX_error_code result;

    result = ktxTexture2_CreateFromNamedFile(filePath.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);

    if (result != KTX_SUCCESS) {
        throw std::runtime_error("Failed to load KTX2 texture!");
    }

    vk::DeviceSize imageSize = kTexture->dataSize;
    vk::Format format = static_cast<vk::Format>(kTexture->vkFormat);

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;
    createBuffer(ctx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* data = ctx->getDevice().mapMemory(stagingBufferMemory, 0, imageSize);
    memcpy(data, kTexture->pData, static_cast<size_t>(imageSize));
    ctx->getDevice().unmapMemory(stagingBufferMemory);

    createImage(ctx, kTexture->baseWidth, kTexture->baseHeight, format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);

    transitionImageLayout(ctx, format, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    copyBufferToImage(ctx, stagingBuffer, kTexture->baseWidth, kTexture->baseHeight);
    transitionImageLayout(ctx, format, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    ctx->getDevice().destroyBuffer(stagingBuffer);
    ctx->getDevice().freeMemory(stagingBufferMemory);

    createImageView(ctx, format);
    createSampler(ctx);

    ktxTexture_Destroy(ktxTexture(kTexture));
}

void Texture::cleanup() {
    if (context) {
        auto device = context->getDevice();
        if (sampler) {
            spdlog::debug("Destroying texture sampler");
            device.destroySampler(sampler);
            sampler = nullptr;
        }
        if (imageView) {
            spdlog::debug("Destroying texture image view");
            device.destroyImageView(imageView);
            imageView = nullptr;
        }
        if (image) {
            spdlog::debug("Destroying texture image");
            device.destroyImage(image);
            image = nullptr;
        }
        if (imageMemory) {
            spdlog::debug("Freeing texture image memory");
            device.freeMemory(imageMemory);
            imageMemory = nullptr;
        }
    }
}

void Texture::createImage(VulkanContext* ctx, uint32_t width, uint32_t height,
                          vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                          vk::MemoryPropertyFlags properties) {
    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = usage;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    image = ctx->getDevice().createImage(imageInfo);

    vk::MemoryRequirements memRequirements = ctx->getDevice().getImageMemoryRequirements(image);
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(ctx, memRequirements.memoryTypeBits, properties);

    imageMemory = ctx->getDevice().allocateMemory(allocInfo);
    ctx->getDevice().bindImageMemory(image, imageMemory, 0);
}

void Texture::createImageView(VulkanContext* ctx, vk::Format format) {
    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    imageView = ctx->getDevice().createImageView(viewInfo);
}

void Texture::createSampler(VulkanContext* ctx) {
    vk::PhysicalDeviceProperties properties = ctx->getPhysicalDevice().getProperties();

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

    sampler = ctx->getDevice().createSampler(samplerInfo);
}

void Texture::transitionImageLayout(VulkanContext* ctx, vk::Format format,
                                    vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands(ctx);

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, 0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(ctx, commandBuffer);
}

void Texture::copyBufferToImage(VulkanContext* ctx, vk::Buffer buffer, uint32_t width, uint32_t height) {
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands(ctx);

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

    commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, 1, &region);

    endSingleTimeCommands(ctx, commandBuffer);
}

}