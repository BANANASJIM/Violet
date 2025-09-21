#pragma once

#include <vulkan/vulkan.hpp>
#include "GPUResource.hpp"
#include "ResourceFactory.hpp"
#include <EASTL/string.h>

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
        , sampler(eastl::move(other.sampler)) {
        other.imageResource = {};
    }

    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            cleanup();
            GPUResource::operator=(eastl::move(other));
            imageResource = other.imageResource;
            imageView = eastl::move(other.imageView);
            sampler = eastl::move(other.sampler);
            other.imageResource = {};
        }
        return *this;
    }

    void loadFromFile(VulkanContext* context, const eastl::string& filePath);
    void loadFromKTX2(VulkanContext* context, const eastl::string& filePath);
    void loadFromMemory(VulkanContext* context, const unsigned char* data, size_t size, int width, int height, int channels, bool srgb = true);
    void cleanup() override;

    [[nodiscard]] vk::Image getImage() const { return imageResource.image; }
    [[nodiscard]] vk::ImageView getImageView() const { return *imageView; }
    [[nodiscard]] vk::Sampler getSampler() const { return *sampler; }

private:
    void createImageView(VulkanContext* context, vk::Format format);
    void createSampler(VulkanContext* context);
    void transitionImageLayout(VulkanContext* context, vk::Format format, vk::ImageLayout oldLayout,
                               vk::ImageLayout newLayout);

private:
    ImageResource imageResource;
    vk::raii::ImageView imageView{nullptr};
    vk::raii::Sampler sampler{nullptr};
};

} // namespace violet
