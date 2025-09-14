#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>

namespace violet {

class VulkanContext;
class TestTexture;

class Texture {
    friend class TestTexture;
public:
    void loadFromFile(VulkanContext* context, const eastl::string& filePath);
    void loadFromKTX2(VulkanContext* context, const eastl::string& filePath);
    void cleanup();

    vk::Image getImage() const { return image; }
    vk::ImageView getImageView() const { return imageView; }
    vk::Sampler getSampler() const { return sampler; }

private:
    void createImage(VulkanContext* context, uint32_t width, uint32_t height,
                     vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                     vk::MemoryPropertyFlags properties);
    void createImageView(VulkanContext* context, vk::Format format);
    void createSampler(VulkanContext* context);
    void transitionImageLayout(VulkanContext* context, vk::Format format,
                               vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
    void copyBufferToImage(VulkanContext* context, vk::Buffer buffer,
                           uint32_t width, uint32_t height);

private:
    VulkanContext* context = nullptr;
    vk::Image image = nullptr;
    vk::DeviceMemory imageMemory = nullptr;
    vk::ImageView imageView = nullptr;
    vk::Sampler sampler = nullptr;
};

}