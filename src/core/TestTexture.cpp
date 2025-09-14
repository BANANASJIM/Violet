#include "TestTexture.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/Buffer.hpp"

namespace violet {

void TestTexture::createCheckerboardTexture(VulkanContext* context, Texture& texture, uint32_t width, uint32_t height) {
    // Create checkerboard pattern
    eastl::vector<uint8_t> pixels(width * height * 4);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            bool checker = ((x / 32) + (y / 32)) % 2;
            uint32_t index = (y * width + x) * 4;

            if (checker) {
                pixels[index + 0] = 255;  // R
                pixels[index + 1] = 255;  // G
                pixels[index + 2] = 255;  // B
            } else {
                pixels[index + 0] = 64;   // R
                pixels[index + 1] = 64;   // G
                pixels[index + 2] = 64;   // B
            }
            pixels[index + 3] = 255;  // A
        }
    }

    vk::DeviceSize imageSize = pixels.size();

    // Create staging buffer
    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;
    createBuffer(context, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* data = context->getDevice().mapMemory(stagingBufferMemory, 0, imageSize);
    memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
    context->getDevice().unmapMemory(stagingBufferMemory);

    // Set context and create texture using the existing methods
    texture.context = context;
    texture.createImage(context, width, height, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                        vk::MemoryPropertyFlagBits::eDeviceLocal);

    texture.transitionImageLayout(context, vk::Format::eR8G8B8A8Srgb,
                                  vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    texture.copyBufferToImage(context, stagingBuffer, width, height);
    texture.transitionImageLayout(context, vk::Format::eR8G8B8A8Srgb,
                                  vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    context->getDevice().destroyBuffer(stagingBuffer);
    context->getDevice().freeMemory(stagingBufferMemory);

    texture.createImageView(context, vk::Format::eR8G8B8A8Srgb);
    texture.createSampler(context);
}

}