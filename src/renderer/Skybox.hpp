#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/unique_ptr.h>
#include <EASTL/array.h>
#include <EASTL/string.h>

namespace violet {

class VulkanContext;
class RenderPass;
class Material;
class Texture;

class Skybox {
public:
    Skybox() = default;
    ~Skybox();

    // Delete copy operations
    Skybox(const Skybox&) = delete;
    Skybox& operator=(const Skybox&) = delete;

    // Enable move operations
    Skybox(Skybox&& other) noexcept;
    Skybox& operator=(Skybox&& other) noexcept;

    void init(VulkanContext* context, RenderPass* renderPass, class ForwardRenderer* renderer);
    void cleanup();

    // Texture management
    void loadCubemap(VulkanContext* context, const eastl::array<eastl::string, 6>& facePaths);
    void setTexture(eastl::unique_ptr<Texture> texture);

    // Parameter management
    void setExposure(float exposure) { this->exposure = exposure; }
    void setRotation(float rotation) { this->rotation = rotation; }
    void setEnabled(bool enabled) { this->enabled = enabled && (texture != nullptr); }

    float getExposure() const { return exposure; }
    float getRotation() const { return rotation; }
    bool isEnabled() const { return enabled; }

    // Rendering
    void render(vk::CommandBuffer commandBuffer, uint32_t frameIndex,
                vk::PipelineLayout pipelineLayout, vk::DescriptorSet globalDescriptorSet);

    // Texture access for descriptor set binding
    Texture* getTexture() const { return texture.get(); }

    // Material access
    Material* getMaterial() const { return material; }

private:
    VulkanContext* context = nullptr;
    RenderPass* renderPass = nullptr;
    class ForwardRenderer* renderer = nullptr;

    Material* material = nullptr;
    eastl::unique_ptr<Texture> texture = nullptr;

    float exposure = 1.0f;
    float rotation = 0.0f;
    bool enabled = false;
};

} // namespace violet