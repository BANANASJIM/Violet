#include "core/Exception.hpp"
#include "Texture.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <ktx.h>
#include <stb_image.h>

#include "core/Exception.hpp"

#include "Buffer.hpp"
#include "VulkanContext.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"

namespace violet {

void Texture::loadFromFile(VulkanContext* ctx, const eastl::string& filePath) {
    context = ctx;

    eastl::string resolvedPath = FileSystem::resolveRelativePath(filePath);
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(resolvedPath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    vk::DeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) {
        throw RuntimeError("Failed to load texture image!");
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

    eastl::string resolvedPath = FileSystem::resolveRelativePath(filePath);
    ktxTexture2* kTexture;
    KTX_error_code result;

    result = ktxTexture2_CreateFromNamedFile(resolvedPath.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);

    if (result != KTX_SUCCESS) {
        throw RuntimeError("Failed to load KTX2 texture!");
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

void Texture::loadFromMemory(VulkanContext* ctx, const unsigned char* data, size_t size, int width, int height, int channels, bool srgb) {
    context = ctx;

    // Calculate actual size needed
    int requiredChannels = 4; // Always convert to RGBA
    vk::DeviceSize imageSize = width * height * requiredChannels;

    // Create staging buffer using ResourceFactory
    BufferInfo stagingBufferInfo;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingBufferInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingBufferInfo.debugName = "Texture memory staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(ctx, stagingBufferInfo);

    void* mappedData = ResourceFactory::mapBuffer(ctx, stagingBuffer);

    // Convert to RGBA if needed
    unsigned char* dst = static_cast<unsigned char*>(mappedData);
    if (channels == 4) {
        memcpy(dst, data, static_cast<size_t>(imageSize));
    } else if (channels == 3) {
        for (int i = 0; i < width * height; i++) {
            dst[i * 4 + 0] = data[i * 3 + 0];
            dst[i * 4 + 1] = data[i * 3 + 1];
            dst[i * 4 + 2] = data[i * 3 + 2];
            dst[i * 4 + 3] = 255;
        }
    } else if (channels == 2) {
        for (int i = 0; i < width * height; i++) {
            dst[i * 4 + 0] = data[i * 2 + 0];
            dst[i * 4 + 1] = data[i * 2 + 0];
            dst[i * 4 + 2] = data[i * 2 + 0];
            dst[i * 4 + 3] = data[i * 2 + 1];
        }
    } else if (channels == 1) {
        for (int i = 0; i < width * height; i++) {
            dst[i * 4 + 0] = data[i];
            dst[i * 4 + 1] = data[i];
            dst[i * 4 + 2] = data[i];
            dst[i * 4 + 3] = 255;
        }
    }


    // Create image using ResourceFactory
    vk::Format format = srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
    ImageInfo imageInfo;
    imageInfo.width = static_cast<uint32_t>(width);
    imageInfo.height = static_cast<uint32_t>(height);
    imageInfo.format = format;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.debugName = "Texture from memory";

    imageResource = ResourceFactory::createImage(ctx, imageInfo);
    allocation = imageResource.allocation;

    transitionImageLayout(ctx, format, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    ResourceFactory::copyBufferToImage(ctx, stagingBuffer, imageResource,
                                       static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    transitionImageLayout(ctx, format, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    ResourceFactory::destroyBuffer(ctx, stagingBuffer);

    createImageView(ctx, format);
    createSampler(ctx);
}

void Texture::loadCubemap(VulkanContext* ctx, const eastl::array<eastl::string, 6>& facePaths) {
    context = ctx;
    isCubemapTexture = true;
    format = vk::Format::eR8G8B8A8Srgb;  // Set default format for non-HDR cubemaps

    violet::Log::info("Renderer", "Loading cubemap with 6 faces");

    // For now, create a simple placeholder cubemap texture
    // TODO: Implement proper cubemap loading from files

    // Create a larger placeholder cubemap with gradient colors for better visibility
    constexpr uint32_t faceSize = 256;  // Larger size for better testing
    constexpr uint32_t channels = 4;
    constexpr uint32_t faceDataSize = faceSize * faceSize * channels;
    constexpr uint32_t totalSize = faceDataSize * 6;

    eastl::vector<uint8_t> cubemapData(totalSize);

    // More vibrant base colors for each face (RGBA)
    uint8_t faceColors[6][4] = {
        {255, 100, 100, 255},  // +X (right) - bright red
        {100, 255, 100, 255},  // -X (left) - bright green
        {100, 100, 255, 255},  // +Y (top) - bright blue
        {255, 255, 100, 255},  // -Y (bottom) - bright yellow
        {255, 100, 255, 255},  // +Z (front) - bright magenta
        {100, 255, 255, 255}   // -Z (back) - bright cyan
    };

    // Create gradient patterns for each face to make them more visible
    for (int face = 0; face < 6; ++face) {
        uint8_t* faceData = cubemapData.data() + face * faceDataSize;
        uint8_t baseR = faceColors[face][0];
        uint8_t baseG = faceColors[face][1];
        uint8_t baseB = faceColors[face][2];

        for (uint32_t y = 0; y < faceSize; ++y) {
            for (uint32_t x = 0; x < faceSize; ++x) {
                uint32_t pixelIndex = (y * faceSize + x) * channels;

                // Create gradient based on position
                float u = (float)x / (float)faceSize;
                float v = (float)y / (float)faceSize;
                float intensity = 0.5f + 0.5f * (u + v) * 0.5f;  // Gradient from 0.5 to 1.0

                faceData[pixelIndex + 0] = (uint8_t)(baseR * intensity);  // R
                faceData[pixelIndex + 1] = (uint8_t)(baseG * intensity);  // G
                faceData[pixelIndex + 2] = (uint8_t)(baseB * intensity);  // B
                faceData[pixelIndex + 3] = 255;                           // A
            }
        }
    }

    // Create staging buffer
    BufferInfo stagingBufferInfo;
    stagingBufferInfo.size = totalSize;
    stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingBufferInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingBufferInfo.debugName = "Cubemap staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(ctx, stagingBufferInfo);

    void* data = ResourceFactory::mapBuffer(ctx, stagingBuffer);
    memcpy(data, cubemapData.data(), totalSize);

    // Create cubemap image
    ImageInfo imageInfo;
    imageInfo.width = faceSize;
    imageInfo.height = faceSize;
    imageInfo.format = vk::Format::eR8G8B8A8Srgb;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.arrayLayers = 6;
    imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    imageInfo.debugName = "Skybox Cubemap";

    imageResource = ResourceFactory::createImage(ctx, imageInfo);
    allocation = imageResource.allocation;

    // Transition to transfer dst
    transitionCubemapLayout(ctx, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

    // Copy buffer to image for each face
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands(ctx);

    for (uint32_t face = 0; face < 6; ++face) {
        vk::BufferImageCopy region;
        region.bufferOffset = face * faceDataSize;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = face;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{faceSize, faceSize, 1};

        commandBuffer.copyBufferToImage(stagingBuffer.buffer, imageResource.image,
                                       vk::ImageLayout::eTransferDstOptimal, region);
    }

    endSingleTimeCommands(ctx, commandBuffer);

    // Transition to shader read
    transitionCubemapLayout(ctx, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    // Clean up staging buffer
    ResourceFactory::destroyBuffer(ctx, stagingBuffer);

    // Create image view and sampler for cubemap
    createCubemapImageView(ctx);
    createSampler(ctx);

    violet::Log::info("Renderer", "Cubemap texture loaded successfully");
}

void Texture::createEmptyCubemap(VulkanContext* ctx, uint32_t size, vk::Format fmt, vk::ImageUsageFlags usage) {
    context = ctx;
    format = fmt;
    isCubemapTexture = true;

    // Create empty cubemap image with specified usage
    ImageInfo imageInfo;
    imageInfo.width = size;
    imageInfo.height = size;
    imageInfo.format = format;
    imageInfo.usage = usage;  // Caller specifies usage (e.g., storage + sampled)
    imageInfo.arrayLayers = 6;
    imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    imageInfo.debugName = "Empty Cubemap";

    imageResource = ResourceFactory::createImage(context, imageInfo);
    allocation = imageResource.allocation;

    createCubemapImageView(context);
    createSampler(context);

    violet::Log::info("Renderer", "Empty cubemap created: {}x{}", size, size);
}

void Texture::loadHDR(VulkanContext* ctx, const eastl::string& hdrPath) {
    context = ctx;

    eastl::string resolvedPath = FileSystem::resolveRelativePath(hdrPath);
    violet::Log::info("Renderer", "Loading HDR texture from: {}", resolvedPath.c_str());

    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    float* pixels = stbi_loadf(resolvedPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        violet::Log::error("Renderer", "Failed to load HDR file: {}", hdrPath.c_str());
        return;
    }

    vk::DeviceSize imageSize = width * height * 4 * sizeof(float);
    format = vk::Format::eR16G16B16A16Sfloat; // HDR format

    // Create staging buffer
    BufferInfo stagingBufferInfo;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingBufferInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingBufferInfo.debugName = "HDR staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(ctx, stagingBufferInfo);

    void* data = ResourceFactory::mapBuffer(ctx, stagingBuffer);

    // Convert float data to half float for R16G16B16A16
    uint16_t* halfData = static_cast<uint16_t*>(data);
    for (int i = 0; i < width * height * 4; ++i) {
        // Simple float to half conversion (can be optimized)
        float value = pixels[i];
        uint32_t fbits = *reinterpret_cast<uint32_t*>(&value);
        uint16_t sign = (fbits >> 31) & 0x1;
        int32_t exp = ((fbits >> 23) & 0xFF) - 127 + 15;
        uint32_t mantissa = fbits & 0x7FFFFF;

        if (exp <= 0) {
            halfData[i] = sign << 15;
        } else if (exp >= 31) {
            halfData[i] = (sign << 15) | 0x7C00;
        } else {
            halfData[i] = (sign << 15) | (exp << 10) | (mantissa >> 13);
        }
    }

    stbi_image_free(pixels);

    // Create image
    ImageInfo imageInfo;
    imageInfo.width = width;
    imageInfo.height = height;
    imageInfo.format = format;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.debugName = hdrPath;

    imageResource = ResourceFactory::createImage(ctx, imageInfo);
    allocation = imageResource.allocation;

    transitionImageLayout(ctx, format, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    ResourceFactory::copyBufferToImage(ctx, stagingBuffer, imageResource, width, height);
    transitionImageLayout(ctx, format, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    ResourceFactory::destroyBuffer(ctx, stagingBuffer);

    createImageView(ctx, format);
    createSampler(ctx);

    violet::Log::info("Renderer", "HDR texture loaded successfully: {}x{}", width, height);
}

void Texture::loadEquirectangularToCubemap(VulkanContext* ctx, const eastl::string& hdrPath) {
    context = ctx;
    isCubemapTexture = true;

    eastl::string resolvedPath = FileSystem::resolveRelativePath(hdrPath);
    violet::Log::info("Renderer", "Loading HDR equirectangular map and converting to cubemap: {}", resolvedPath.c_str());
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    float* pixels = stbi_loadf(resolvedPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        violet::Log::error("Renderer", "Failed to load HDR file: {}", hdrPath.c_str());
        return;
    }

    // For now, create a simple cubemap with fixed size
    const uint32_t cubemapSize = 512;
    const uint32_t faceDataSize = cubemapSize * cubemapSize * 4 * sizeof(uint16_t); // R16G16B16A16
    const uint32_t totalSize = faceDataSize * 6;

    format = vk::Format::eR16G16B16A16Sfloat;

    // Allocate memory for all 6 faces
    eastl::vector<uint16_t> cubemapData(totalSize / sizeof(uint16_t));

    // Convert equirectangular to cubemap faces
    // This is a simplified version - a full implementation would use proper spherical mapping
    for (int face = 0; face < 6; ++face) {
        for (uint32_t y = 0; y < cubemapSize; ++y) {
            for (uint32_t x = 0; x < cubemapSize; ++x) {
                // Calculate direction vector for this pixel on the cube face
                float u = (x + 0.5f) / cubemapSize * 2.0f - 1.0f;
                float v = (y + 0.5f) / cubemapSize * 2.0f - 1.0f;

                glm::vec3 dir;
                switch (face) {
                    case 0: dir = glm::vec3( 1.0f, -v, -u); break; // +X
                    case 1: dir = glm::vec3(-1.0f, -v,  u); break; // -X
                    case 2: dir = glm::vec3( u,  1.0f,  v); break; // +Y
                    case 3: dir = glm::vec3( u, -1.0f, -v); break; // -Y
                    case 4: dir = glm::vec3( u, -v,  1.0f); break; // +Z
                    case 5: dir = glm::vec3(-u, -v, -1.0f); break; // -Z
                }
                dir = glm::normalize(dir);

                // Convert direction to equirectangular coordinates
                float theta = atan2f(dir.z, dir.x);
                float phi = asinf(dir.y);

                float eqU = (theta + glm::pi<float>()) / (2.0f * glm::pi<float>());
                float eqV = (phi + glm::half_pi<float>()) / glm::pi<float>();

                // Sample from equirectangular image
                int eqX = static_cast<int>(eqU * width) % width;
                int eqY = static_cast<int>(eqV * height) % height;
                int srcIdx = (eqY * width + eqX) * 4;

                // Convert to half float and store
                int dstIdx = (face * cubemapSize * cubemapSize + y * cubemapSize + x) * 4;
                for (int c = 0; c < 4; ++c) {
                    float value = pixels[srcIdx + c];
                    // Simple float to half conversion
                    uint32_t fbits = *reinterpret_cast<uint32_t*>(&value);
                    uint16_t sign = (fbits >> 31) & 0x1;
                    int32_t exp = ((fbits >> 23) & 0xFF) - 127 + 15;
                    uint32_t mantissa = fbits & 0x7FFFFF;

                    if (exp <= 0) {
                        cubemapData[dstIdx + c] = sign << 15;
                    } else if (exp >= 31) {
                        cubemapData[dstIdx + c] = (sign << 15) | 0x7C00;
                    } else {
                        cubemapData[dstIdx + c] = (sign << 15) | (exp << 10) | (mantissa >> 13);
                    }
                }
            }
        }
    }

    stbi_image_free(pixels);

    // Create staging buffer
    BufferInfo stagingBufferInfo;
    stagingBufferInfo.size = totalSize;
    stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingBufferInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingBufferInfo.debugName = "HDR Cubemap staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(ctx, stagingBufferInfo);

    void* data = ResourceFactory::mapBuffer(ctx, stagingBuffer);
    memcpy(data, cubemapData.data(), totalSize);

    // Create cubemap image
    ImageInfo imageInfo;
    imageInfo.width = cubemapSize;
    imageInfo.height = cubemapSize;
    imageInfo.format = format;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.arrayLayers = 6;
    imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    imageInfo.debugName = "HDR Environment Cubemap";

    imageResource = ResourceFactory::createImage(ctx, imageInfo);
    allocation = imageResource.allocation;

    // Transition to transfer dst
    transitionCubemapLayout(ctx, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

    // Copy buffer to image for each face
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands(ctx);

    for (uint32_t face = 0; face < 6; ++face) {
        vk::BufferImageCopy region;
        region.bufferOffset = face * faceDataSize;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = face;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{cubemapSize, cubemapSize, 1};

        commandBuffer.copyBufferToImage(stagingBuffer.buffer, imageResource.image,
                                       vk::ImageLayout::eTransferDstOptimal, region);
    }

    endSingleTimeCommands(ctx, commandBuffer);

    // Transition to shader read
    transitionCubemapLayout(ctx, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    // Clean up staging buffer
    ResourceFactory::destroyBuffer(ctx, stagingBuffer);

    // Create image view and sampler for cubemap
    createCubemapImageView(ctx);
    createSampler(ctx);

    violet::Log::info("Renderer", "HDR equirectangular converted to cubemap successfully");
}

void Texture::cleanup() {
    if (context) {
        // Cleaning up texture resources

        // Clean up RAII objects in reverse order
        // Destroying texture RAII objects
        sampler = nullptr;
        imageView = nullptr;

        // Clean up VMA resources
        ResourceFactory::destroyImage(context, imageResource);
        allocation = VK_NULL_HANDLE;
        context = nullptr;

        // Texture cleanup completed
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
    // ImageView created
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
    // Sampler created
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

void Texture::transitionCubemapLayout(VulkanContext* ctx, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
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
    barrier.subresourceRange.layerCount = 6; // 6 faces for cubemap

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

void Texture::createCubemapImageView(VulkanContext* ctx) {
    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = imageResource.image;
    viewInfo.viewType = vk::ImageViewType::eCube;
    viewInfo.format = imageResource.format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6; // 6 faces for cubemap

    imageView = vk::raii::ImageView(ctx->getDeviceRAII(), viewInfo);
}


} // namespace violet
