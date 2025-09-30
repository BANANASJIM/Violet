#include "Skybox.hpp"
#include "VulkanContext.hpp"
#include "RenderPass.hpp"
#include "Material.hpp"
#include "Texture.hpp"
#include "ResourceFactory.hpp"
#include "GraphicsPipeline.hpp"
#include "ForwardRenderer.hpp"
#include "DescriptorSet.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"

namespace violet {

Skybox::~Skybox() {
    cleanup();
}

Skybox::Skybox(Skybox&& other) noexcept
    : context(other.context)
    , renderPass(other.renderPass)
    , renderer(other.renderer)
    , material(other.material)
    , texture(eastl::move(other.texture))
    , exposure(other.exposure)
    , rotation(other.rotation)
    , enabled(other.enabled) {
    other.context = nullptr;
    other.renderPass = nullptr;
    other.renderer = nullptr;
    other.material = nullptr;
    other.exposure = 1.0f;
    other.rotation = 0.0f;
    other.enabled = false;
}

Skybox& Skybox::operator=(Skybox&& other) noexcept {
    if (this != &other) {
        cleanup();
        context = other.context;
        renderPass = other.renderPass;
        material = other.material;
        texture = eastl::move(other.texture);
        exposure = other.exposure;
        rotation = other.rotation;
        enabled = other.enabled;

        other.context = nullptr;
        other.renderPass = nullptr;
        other.exposure = 1.0f;
        other.rotation = 0.0f;
        other.enabled = false;
    }
    return *this;
}

void Skybox::init(VulkanContext* ctx, RenderPass* rp, ForwardRenderer* fwdRenderer) {
    context = ctx;
    renderPass = rp;
    renderer = fwdRenderer;

    // Create skybox material with no vertex input
    PipelineConfig skyboxConfig;
    skyboxConfig.useVertexInput = false;  // Skybox generates vertices procedurally
    skyboxConfig.enableDepthTest = false;  // Skybox should be in background
    skyboxConfig.enableDepthWrite = false;  // Don't write to depth buffer
    skyboxConfig.cullMode = vk::CullModeFlagBits::eFront;  // Cull front faces for inside view

    material = renderer->createMaterial(
        FileSystem::resolveRelativePath("build/shaders/skybox.vert.spv"),
        FileSystem::resolveRelativePath("build/shaders/skybox.frag.spv"),
        DescriptorSetType::GlobalUniforms, skyboxConfig);

    violet::Log::info("Renderer", "Skybox initialized with material");
}

void Skybox::cleanup() {
    // Don't delete material - it's managed by ForwardRenderer
    material = nullptr;
    texture.reset();
    context = nullptr;
    renderPass = nullptr;
}

void Skybox::loadCubemap(VulkanContext* context, const eastl::array<eastl::string, 6>& facePaths) {
    texture = ResourceFactory::createCubemapTexture(context, facePaths);
    enabled = (texture != nullptr);

    if (texture) {
        violet::Log::info("Renderer", "Skybox cubemap loaded successfully");
    } else {
        violet::Log::warn("Renderer", "Failed to load skybox cubemap");
    }
}

void Skybox::setTexture(eastl::unique_ptr<Texture> tex) {
    texture = eastl::move(tex);
    enabled = (texture != nullptr);
}

void Skybox::render(vk::CommandBuffer commandBuffer, uint32_t frameIndex,
                   vk::PipelineLayout pipelineLayout, vk::DescriptorSet globalDescriptorSet) {
    if (!enabled || !texture || !material || !material->getPipeline()) {
        return;
    }

    // Validate texture is fully initialized
    if (!texture->getImageView() || !texture->getSampler()) {
        violet::Log::warn("Renderer", "Skipping skybox render - texture not fully initialized");
        return;
    }

    // Validate descriptor set
    if (!globalDescriptorSet) {
        violet::Log::warn("Renderer", "Skipping skybox render - global descriptor set is invalid");
        return;
    }

    // Bind skybox pipeline
    material->getPipeline()->bind(commandBuffer);

    // Bind global descriptor set which includes the skybox texture
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        0, // set 0 (global set)
        globalDescriptorSet,
        {}
    );

    // Draw full-screen triangle (no vertex buffer needed)
    commandBuffer.draw(3, 1, 0, 0);
}

} // namespace violet