#include "EnvironmentMap.hpp"
#include "VulkanContext.hpp"
#include "RenderPass.hpp"
#include "Material.hpp"
#include "Texture.hpp"
#include "ResourceFactory.hpp"
#include "GraphicsPipeline.hpp"
#include "ComputePipeline.hpp"
#include "ForwardRenderer.hpp"
#include "DescriptorSet.hpp"
#include "Buffer.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include <stb_image.h>

namespace violet {

EnvironmentMap::EnvironmentMap() = default;

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

    // Create compute pipeline for equirectangular to cubemap conversion
    equirectToCubemapPipeline = eastl::make_unique<ComputePipeline>();

    // Create descriptor set for compute shader
    computeDescriptorSet = eastl::make_unique<DescriptorSet>();
    computeDescriptorSet->create(context, 1, DescriptorSetType::EquirectToCubemap);

    // Setup compute pipeline with descriptor set layout
    ComputePipelineConfig computeConfig;
    computeConfig.descriptorSetLayouts.push_back(computeDescriptorSet->getLayout());

    // Add push constant for cubemap size and current face
    vk::PushConstantRange pushConstant;
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t) * 2; // cubemapSize + currentFace
    computeConfig.pushConstantRanges.push_back(pushConstant);

    equirectToCubemapPipeline->init(
        context,
        FileSystem::resolveRelativePath("build/shaders/equirect_to_cubemap.comp.spv"),
        computeConfig
    );

    violet::Log::info("Renderer", "EnvironmentMap initialized with skybox material and compute pipeline");
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

    // Step 1: Load equirectangular HDR texture (2D)
    // Keep it as member variable to prevent premature destruction
    equirectTexture = eastl::make_unique<Texture>();
    equirectTexture->loadHDR(context, resolvedPath);

    if (!equirectTexture->getImageView() || !equirectTexture->getSampler()) {
        violet::Log::error("Renderer", "Failed to load equirectangular HDR texture");
        return;
    }

    // Step 2: Create empty cubemap with storage + sampled usage
    const uint32_t cubemapSize = 512;
    environmentTexture = eastl::make_unique<Texture>();
    environmentTexture->createEmptyCubemap(
        context,
        cubemapSize,
        vk::Format::eR16G16B16A16Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled
    );

    // Step 3: Generate cubemap from equirectangular using compute shader
    generateCubemapFromEquirect(equirectTexture.get(), environmentTexture.get(), cubemapSize);

    // Step 4: IMPORTANT - Free equirect texture immediately after GPU work completes
    // The compute shader has finished (endSingleTimeCommands waits for GPU),
    // so we can safely free the equirect texture to prevent validation errors
    // when loading a new HDR file later
    equirectTexture.reset();

    currentType = Type::Cubemap;  // Output is cubemap
    params.enabled = true;

    // Step 4: Update the global descriptor set with the new environment texture
    if (renderer && environmentTexture) {
        // Ensure texture is fully initialized before setting it
        if (environmentTexture->getImageView() && environmentTexture->getSampler()) {
            renderer->getGlobalUniforms().setSkyboxTexture(environmentTexture.get());
            violet::Log::info("Renderer", "Successfully set HDR environment cubemap in global uniforms");
        } else {
            violet::Log::error("Renderer", "HDR environment cubemap not fully initialized - cannot set in descriptor set");
        }
    }

    violet::Log::info("Renderer", "HDR environment map loaded and converted to cubemap via compute shader");
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

void EnvironmentMap::generateCubemapFromEquirect(Texture* equirectTexture, Texture* cubemapTexture, uint32_t cubemapSize) {
    if (!equirectTexture || !cubemapTexture || !equirectToCubemapPipeline || !computeDescriptorSet) {
        violet::Log::error("Renderer", "Invalid parameters for generateCubemapFromEquirect");
        return;
    }

    violet::Log::info("Renderer", "Generating cubemap from equirectangular texture using compute shader (size: {})", cubemapSize);

    // Update descriptor set with input and output textures
    computeDescriptorSet->updateTexture(0, equirectTexture, 0);  // Binding 0: input sampler2D
    computeDescriptorSet->updateStorageImage(0, cubemapTexture, 1);  // Binding 1: output imageCube

    // Create command buffer for compute operation
    ResourceFactory::executeSingleTimeCommands(context, [&](vk::CommandBuffer cmd) {
        // Transition cubemap to general layout for storage image writes
        vk::ImageMemoryBarrier barrier;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eGeneral;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = cubemapTexture->getImage();
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 6;  // All 6 cubemap faces

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        // Bind compute pipeline
        equirectToCubemapPipeline->bind(cmd);

        // Bind descriptor set
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            equirectToCubemapPipeline->getPipelineLayout(),
            0,
            computeDescriptorSet->getDescriptorSet(0),
            {}
        );

        // Dispatch compute shader for each cubemap face
        struct PushConstants {
            uint32_t cubemapSize;
            uint32_t currentFace;
        } pushConstants;

        pushConstants.cubemapSize = cubemapSize;

        // Calculate workgroup count (16x16 local size in shader)
        uint32_t workgroupCountX = (cubemapSize + 15) / 16;
        uint32_t workgroupCountY = (cubemapSize + 15) / 16;

        for (uint32_t face = 0; face < 6; ++face) {
            pushConstants.currentFace = face;

            // Push constants for current face
            cmd.pushConstants(
                equirectToCubemapPipeline->getPipelineLayout(),
                vk::ShaderStageFlagBits::eCompute,
                0,
                sizeof(PushConstants),
                &pushConstants
            );

            // Dispatch compute workgroups
            equirectToCubemapPipeline->dispatch(cmd, workgroupCountX, workgroupCountY, 1);

            // Insert barrier between faces to ensure proper ordering
            if (face < 5) {
                vk::MemoryBarrier memBarrier;
                memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
                memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {},
                    1, &memBarrier,
                    0, nullptr,
                    0, nullptr
                );
            }
        }

        // Transition cubemap to shader read-only optimal for rendering
        vk::ImageMemoryBarrier finalBarrier;
        finalBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        finalBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        finalBarrier.oldLayout = vk::ImageLayout::eGeneral;
        finalBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        finalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        finalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        finalBarrier.image = cubemapTexture->getImage();
        finalBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        finalBarrier.subresourceRange.baseMipLevel = 0;
        finalBarrier.subresourceRange.levelCount = 1;
        finalBarrier.subresourceRange.baseArrayLayer = 0;
        finalBarrier.subresourceRange.layerCount = 6;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eFragmentShader,
            {},
            0, nullptr,
            0, nullptr,
            1, &finalBarrier
        );
    });

    violet::Log::info("Renderer", "Cubemap generation complete");
}

} // namespace violet