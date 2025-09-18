#include "Texture.hpp"

#include "core/Log.hpp"

#include <ktx.h>
#include <stb_image.h>

#include <stdexcept>

#include "Buffer.hpp"
#include "VulkanContext.hpp"

namespace violet {

void Texture::loadFromFile(VulkanContext* ctx, const eastl::string& filePath) {
    context = ctx;

    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    vk::DeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) {
        throw std::runtime_error("Failed to load texture image!");
    }

    // Create staging buffer using ResourceFactory
    BufferInfo stagingBufferInfo;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingBufferInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingBufferInfo.debugName = "Texture staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(ctx, stagingBufferInfo);

    void* data = ResourceFactory::mapBuffer(ctx, stagingBuffer);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    ResourceFactory::unmapBuffer(ctx, stagingBuffer);

    stbi_image_free(pixels);

    // Create image using ResourceFactory
    ImageInfo imageInfo;
    imageInfo.width = static_cast<uint32_t>(texWidth);
    imageInfo.height = static_cast<uint32_t>(texHeight);
    imageInfo.format = vk::Format::eR8G8B8A8Srgb;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.debugName = filePath;

    imageResource = ResourceFactory::createImage(ctx, imageInfo);
    allocation = imageResource.allocation;

    transitionImageLayout(ctx, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal);
    ResourceFactory::copyBufferToImage(ctx, stagingBuffer, imageResource,
                                       static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    transitionImageLayout(ctx, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal);

    ResourceFactory::destroyBuffer(ctx, stagingBuffer);

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

    // Create staging buffer using ResourceFactory
    BufferInfo stagingBufferInfo;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingBufferInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingBufferInfo.debugName = "KTX2 staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(ctx, stagingBufferInfo);

    void* data = ResourceFactory::mapBuffer(ctx, stagingBuffer);
    memcpy(data, kTexture->pData, static_cast<size_t>(imageSize));
    ResourceFactory::unmapBuffer(ctx, stagingBuffer);

    // Create image using ResourceFactory
    ImageInfo imageInfo;
    imageInfo.width = kTexture->baseWidth;
    imageInfo.height = kTexture->baseHeight;
    imageInfo.format = format;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.debugName = filePath;

    imageResource = ResourceFactory::createImage(ctx, imageInfo);
    allocation = imageResource.allocation;

    transitionImageLayout(ctx, format, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    ResourceFactory::copyBufferToImage(ctx, stagingBuffer, imageResource, kTexture->baseWidth, kTexture->baseHeight);
    transitionImageLayout(ctx, format, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    ResourceFactory::destroyBuffer(ctx, stagingBuffer);

    createImageView(ctx, format);
    createSampler(ctx);

    ktxTexture_Destroy(ktxTexture(kTexture));
}

void Texture::cleanup() {
    if (context) {
        // Clean up RAII objects
        sampler = nullptr;
        imageView = nullptr;

        // Clean up VMA resources
        ResourceFactory::destroyImage(context, imageResource);
        allocation = VK_NULL_HANDLE;
        context = nullptr;
    }
}


void Texture::createImageView(VulkanContext* ctx, vk::Format format) {
    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = imageResource.image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    imageView = vk::raii::ImageView(ctx->getDeviceRAII(), viewInfo);
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

    sampler = vk::raii::Sampler(ctx->getDeviceRAII(), samplerInfo);
}

void Texture::transitionImageLayout(VulkanContext* ctx, vk::Format format, vk::ImageLayout oldLayout,
                                    vk::ImageLayout newLayout) {
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands(ctx);

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = imageResource.image;
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
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
               newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
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


} // namespace violet
