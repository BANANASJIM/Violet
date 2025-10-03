#pragma once

#include <vulkan/vulkan.hpp>
#include "resource/gpu/GPUResource.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include <EASTL/string.h>
#include <EASTL/array.h>

namespace violet {

class VulkanContext;
class TestTexture;

class Texture : public GPUResource {
    friend class TestTexture;

public:
    Texture() = default;
    ~Texture() override { cleanup(); }

    // Delete copy operations
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Enable move operations
    Texture(Texture&& other) noexcept
        : GPUResource(eastl::move(other))
        , imageResource(other.imageResource)
        , imageView(eastl::move(other.imageView))
        , sampler(eastl::move(other.sampler))
        , isCubemapTexture(other.isCubemapTexture) {
        other.imageResource = {};
        other.isCubemapTexture = false;
    }

    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            cleanup();
            GPUResource::operator=(eastl::move(other));
            imageResource = other.imageResource;
            imageView = eastl::move(other.imageView);
            sampler = eastl::move(other.sampler);
            isCubemapTexture = other.isCubemapTexture;
            other.imageResource = {};
            other.isCubemapTexture = false;
        }
        return *this;
    }

    void loadFromFile(VulkanContext* context, const eastl::string& filePath, bool enableMipmaps = false);
    void loadFromKTX2(VulkanContext* context, const eastl::string& filePath, bool enableMipmaps = false);
    void loadFromMemory(VulkanContext* context, const unsigned char* data, size_t size, int width, int height, int channels, bool srgb = true, bool enableMipmaps = false);

    // HDR support
    void loadHDR(VulkanContext* context, const eastl::string& hdrPath);
    void loadEquirectangularToCubemap(VulkanContext* context, const eastl::string& hdrPath);

    // Cubemap support
    void loadCubemap(VulkanContext* context, const eastl::array<eastl::string, 6>& facePaths);
    void loadCubemapFromMemory(VulkanContext* context, const eastl::array<const unsigned char*, 6>& faceData,
                               const eastl::array<size_t, 6>& faceSizes, int faceWidth, int faceHeight, int channels);
    void createEmptyCubemap(VulkanContext* context, uint32_t size, vk::Format format, vk::ImageUsageFlags usage);

    void cleanup() override;

    [[nodiscard]] vk::Image getImage() const { return imageResource.image; }
    [[nodiscard]] vk::ImageView getImageView() const { return *imageView; }
    [[nodiscard]] vk::Sampler getSampler() const { return sampler; }
    [[nodiscard]] bool isCubemap() const { return isCubemapTexture; }
    [[nodiscard]] bool isHDR() const { return format == vk::Format::eR16G16B16A16Sfloat || format == vk::Format::eR32G32B32A32Sfloat; }
    [[nodiscard]] vk::Format getFormat() const { return format; }
    [[nodiscard]] uint32_t getMipLevels() const { return mipLevels; }

    // Sampler management - set from external source (DescriptorManager)
    void setSampler(vk::Sampler externalSampler) { sampler = externalSampler; }

private:
    void createImageView(VulkanContext* context, vk::Format format);
    void transitionImageLayout(VulkanContext* context, vk::Format format, vk::ImageLayout oldLayout,
                               vk::ImageLayout newLayout);
    void createCubemapImageView(VulkanContext* context);
    void transitionCubemapLayout(VulkanContext* context, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);

    // Mipmap generation
    void generateMipmaps(VulkanContext* context, vk::Format format, uint32_t width, uint32_t height, uint32_t arrayLayers = 1);
    static uint32_t calculateMipLevels(uint32_t width, uint32_t height);

private:
    ImageResource imageResource;
    vk::raii::ImageView imageView{nullptr};
    vk::Sampler sampler = VK_NULL_HANDLE;  // External sampler from DescriptorManager (not owned)
    bool isCubemapTexture = false;
    vk::Format format = vk::Format::eR8G8B8A8Srgb;
    uint32_t mipLevels = 1;
};

} // namespace violet
