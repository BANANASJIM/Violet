#include "TestTexture.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/Buffer.hpp"
#include "renderer/ResourceFactory.hpp"

namespace violet {

void TestTexture::createCheckerboardTexture(VulkanContext* context, Texture& texture, uint32_t width, uint32_t height) {
    eastl::vector<uint8_t> pixels(width * height * 4);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            bool checker = ((x / 32) + (y / 32)) % 2;
            uint32_t index = (y * width + x) * 4;

            if (checker) {
                pixels[index + 0] = 255;
                pixels[index + 1] = 255;
                pixels[index + 2] = 255;
            } else {
                pixels[index + 0] = 64;
                pixels[index + 1] = 64;
                pixels[index + 2] = 64;
            }
            pixels[index + 3] = 255;
        }
    }

    vk::DeviceSize imageSize = pixels.size();

    BufferInfo stagingInfo;
    stagingInfo.size = imageSize;
    stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingInfo.debugName = "Checkerboard staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(context, stagingInfo);

    void* data = ResourceFactory::mapBuffer(context, stagingBuffer);
    memcpy(data, pixels.data(), static_cast<size_t>(imageSize));

    texture.context = context;

    ImageInfo imageInfo;
    imageInfo.width = width;
    imageInfo.height = height;
    imageInfo.format = vk::Format::eR8G8B8A8Srgb;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.debugName = "Checkerboard texture";

    texture.imageResource = ResourceFactory::createImage(context, imageInfo);
    texture.allocation = texture.imageResource.allocation;

    texture.transitionImageLayout(context, vk::Format::eR8G8B8A8Srgb,
                                  vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    ResourceFactory::copyBufferToImage(context, stagingBuffer, texture.imageResource, width, height);
    texture.transitionImageLayout(context, vk::Format::eR8G8B8A8Srgb,
                                  vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    ResourceFactory::destroyBuffer(context, stagingBuffer);

    texture.createImageView(context, vk::Format::eR8G8B8A8Srgb);
    texture.createSampler(context);
}

void TestTexture::createWhiteTexture(VulkanContext* context, Texture& texture) {
    constexpr uint32_t width = 4;
    constexpr uint32_t height = 4;
    eastl::vector<uint8_t> pixels(width * height * 4, 255);

    vk::DeviceSize imageSize = pixels.size();

    BufferInfo stagingInfo;
    stagingInfo.size = imageSize;
    stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingInfo.debugName = "White texture staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(context, stagingInfo);

    void* data = ResourceFactory::mapBuffer(context, stagingBuffer);
    memcpy(data, pixels.data(), static_cast<size_t>(imageSize));

    texture.context = context;

    ImageInfo imageInfo;
    imageInfo.width = width;
    imageInfo.height = height;
    imageInfo.format = vk::Format::eR8G8B8A8Srgb;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.debugName = "White texture";

    texture.imageResource = ResourceFactory::createImage(context, imageInfo);
    texture.allocation = texture.imageResource.allocation;

    texture.transitionImageLayout(context, vk::Format::eR8G8B8A8Srgb,
                                  vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    ResourceFactory::copyBufferToImage(context, stagingBuffer, texture.imageResource, width, height);
    texture.transitionImageLayout(context, vk::Format::eR8G8B8A8Srgb,
                                  vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    ResourceFactory::destroyBuffer(context, stagingBuffer);

    texture.createImageView(context, vk::Format::eR8G8B8A8Srgb);
    texture.createSampler(context);
}

}