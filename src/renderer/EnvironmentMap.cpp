#include "EnvironmentMap.hpp"
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
#include <stb_image.h>

namespace violet {

EnvironmentMap::~EnvironmentMap() {
    cleanup();
}

EnvironmentMap::EnvironmentMap(EnvironmentMap&& other) noexcept
    : context(other.context)
    , renderPass(other.renderPass)
    , renderer(other.renderer)
    , environmentTexture(eastl::move(other.environmentTexture))
    , irradianceMap(eastl::move(other.irradianceMap))
    , prefilteredMap(eastl::move(other.prefilteredMap))
    , brdfLUT(eastl::move(other.brdfLUT))
    , skyboxMaterial(other.skyboxMaterial)
    , params(other.params)
    , currentType(other.currentType) {
    other.context = nullptr;
    other.renderPass = nullptr;
    other.renderer = nullptr;
    other.skyboxMaterial = nullptr;
    other.params = {};
    other.currentType = Type::Cubemap;
}

EnvironmentMap& EnvironmentMap::operator=(EnvironmentMap&& other) noexcept {
    if (this != &other) {
        cleanup();
        context = other.context;
        renderPass = other.renderPass;
        renderer = other.renderer;
        environmentTexture = eastl::move(other.environmentTexture);
        irradianceMap = eastl::move(other.irradianceMap);
        prefilteredMap = eastl::move(other.prefilteredMap);
        brdfLUT = eastl::move(other.brdfLUT);
        skyboxMaterial = other.skyboxMaterial;
        params = other.params;
        currentType = other.currentType;

        other.context = nullptr;
        other.renderPass = nullptr;
        other.renderer = nullptr;
        other.skyboxMaterial = nullptr;
        other.params = {};
        other.currentType = Type::Cubemap;
    }
    return *this;
}

void EnvironmentMap::init(VulkanContext* ctx, RenderPass* rp, ForwardRenderer* fwdRenderer) {
    context = ctx;
    renderPass = rp;
    renderer = fwdRenderer;

    // Create skybox material with no vertex input
    PipelineConfig skyboxConfig;
    skyboxConfig.useVertexInput = false;  // Skybox generates vertices procedurally
    skyboxConfig.enableDepthTest = false;  // Skybox should be in background
    skyboxConfig.enableDepthWrite = false;  // Don't write to depth buffer
    skyboxConfig.cullMode = vk::CullModeFlagBits::eFront;  // Cull front faces for inside view

    skyboxMaterial = renderer->createMaterial(
        FileSystem::resolveRelativePath("build/shaders/skybox.vert.spv"),
        FileSystem::resolveRelativePath("build/shaders/skybox.frag.spv"),
        DescriptorSetType::GlobalUniforms, skyboxConfig);

    violet::Log::info("Renderer", "EnvironmentMap initialized with skybox material");
}

void EnvironmentMap::cleanup() {
    // Don't delete material - it's managed by ForwardRenderer
    skyboxMaterial = nullptr;
    environmentTexture.reset();
    irradianceMap.reset();
    prefilteredMap.reset();
    brdfLUT.reset();
    context = nullptr;
    renderPass = nullptr;
    renderer = nullptr;
}

void EnvironmentMap::loadHDR(const eastl::string& hdrPath) {
    if (!context) {
        violet::Log::error("Renderer", "EnvironmentMap not initialized");
        return;
    }

    eastl::string resolvedPath = FileSystem::resolveRelativePath(hdrPath);
    violet::Log::info("Renderer", "Loading HDR environment map from: {}", resolvedPath.c_str());

    // Load HDR file using stb_image
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    float* pixels = stbi_loadf(resolvedPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        violet::Log::error("Renderer", "Failed to load HDR file: {}", resolvedPath.c_str());
        return;
    }

    // For now, create a simple equirectangular to cubemap conversion
    // This will be enhanced later with proper conversion
    // Temporarily create a 2D texture from HDR data

    vk::DeviceSize imageSize = width * height * 4 * sizeof(float);

    // Create staging buffer
    BufferInfo stagingBufferInfo;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingBufferInfo.memoryUsage = MemoryUsage::CPU_TO_GPU;
    stagingBufferInfo.debugName = "HDR staging buffer";

    BufferResource stagingBuffer = ResourceFactory::createBuffer(context, stagingBufferInfo);

    void* data = ResourceFactory::mapBuffer(context, stagingBuffer);
    memcpy(data, pixels, static_cast<size_t>(imageSize));

    stbi_image_free(pixels);

    // Create cubemap texture (for now, we'll create a simple cubemap)
    // In a full implementation, we'd convert equirectangular to cubemap
    const uint32_t cubemapSize = 512; // Fixed size for now

    ImageInfo imageInfo;
    imageInfo.width = cubemapSize;
    imageInfo.height = cubemapSize;
    imageInfo.format = vk::Format::eR16G16B16A16Sfloat; // HDR format
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.arrayLayers = 6;
    imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    imageInfo.debugName = "Environment Cubemap";

    // For now, create a placeholder cubemap
    // Full implementation would convert equirectangular to cubemap faces
    environmentTexture = eastl::make_unique<Texture>();

    // Use the existing loadCubemap with placeholder for now
    // This will be replaced with proper HDR cubemap generation
    eastl::array<eastl::string, 6> placeholderFaces;
    environmentTexture->loadCubemap(context, placeholderFaces);

    ResourceFactory::destroyBuffer(context, stagingBuffer);

    currentType = Type::Equirectangular;
    params.enabled = true;

    // Update the global descriptor set with the new environment texture
    if (renderer && environmentTexture) {
        // Ensure texture is fully initialized before setting it
        if (environmentTexture->getImageView() && environmentTexture->getSampler()) {
            renderer->getGlobalUniforms().setSkyboxTexture(environmentTexture.get());
            violet::Log::info("Renderer", "Successfully set HDR environment texture in global uniforms");
        } else {
            violet::Log::error("Renderer", "HDR environment texture not fully initialized - cannot set in descriptor set");
        }
    }

    violet::Log::info("Renderer", "HDR environment map loaded (placeholder cubemap for now)");
}

void EnvironmentMap::loadCubemap(const eastl::array<eastl::string, 6>& facePaths) {
    environmentTexture = ResourceFactory::createCubemapTexture(context, facePaths);
    params.enabled = (environmentTexture != nullptr);
    currentType = Type::Cubemap;

    // Update the global descriptor set with the new environment texture
    if (renderer && environmentTexture) {
        // Ensure texture is fully initialized before setting it
        if (environmentTexture->getImageView() && environmentTexture->getSampler()) {
            renderer->getGlobalUniforms().setSkyboxTexture(environmentTexture.get());
            violet::Log::info("Renderer", "Successfully set cubemap environment texture in global uniforms");
        } else {
            violet::Log::error("Renderer", "Cubemap environment texture not fully initialized - cannot set in descriptor set");
        }
    }

    if (environmentTexture) {
        violet::Log::info("Renderer", "Environment cubemap loaded successfully");
    } else {
        violet::Log::warn("Renderer", "Failed to load environment cubemap");
    }
}

void EnvironmentMap::setTexture(eastl::unique_ptr<Texture> texture) {
    environmentTexture = eastl::move(texture);
    params.enabled = (environmentTexture != nullptr);
}

void EnvironmentMap::generateIBLMaps() {
    // Future implementation for IBL precomputation
    // Will generate irradianceMap, prefilteredMap, and brdfLUT
    violet::Log::info("Renderer", "IBL map generation not yet implemented");
}

void EnvironmentMap::renderSkybox(vk::CommandBuffer commandBuffer, uint32_t frameIndex,
                                  vk::PipelineLayout pipelineLayout, vk::DescriptorSet globalDescriptorSet) {
    if (!params.enabled || !environmentTexture || !skyboxMaterial || !skyboxMaterial->getPipeline()) {
        return;
    }

    // Validate environment texture is fully initialized
    if (!environmentTexture->getImageView() || !environmentTexture->getSampler()) {
        violet::Log::warn("Renderer", "Skipping skybox render - environment texture not fully initialized");
        return;
    }

    // Validate descriptor set
    if (!globalDescriptorSet) {
        violet::Log::warn("Renderer", "Skipping skybox render - global descriptor set is invalid");
        return;
    }

    // Bind skybox pipeline
    skyboxMaterial->getPipeline()->bind(commandBuffer);

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