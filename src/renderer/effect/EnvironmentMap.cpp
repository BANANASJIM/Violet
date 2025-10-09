#include "renderer/effect/EnvironmentMap.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "resource/Texture.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/MaterialManager.hpp"
#include "resource/TextureManager.hpp"
#include "resource/shader/ShaderLibrary.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/ComputePipeline.hpp"
#include "renderer/vulkan/DescriptorSet.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include <EASTL/unique_ptr.h>

namespace violet {

EnvironmentMap::EnvironmentMap() = default;

EnvironmentMap::~EnvironmentMap() {
    cleanup();
}

EnvironmentMap::EnvironmentMap(EnvironmentMap&& other) noexcept
    : context(other.context)
    , materialManager(other.materialManager)
    , descriptorManager(other.descriptorManager)
    , textureManager(other.textureManager)
    , environmentTextureHandle(other.environmentTextureHandle)
    , irradianceMapHandle(other.irradianceMapHandle)
    , prefilteredMapHandle(other.prefilteredMapHandle)
    , brdfLUTHandle(other.brdfLUTHandle)
    , environmentMapIndex(other.environmentMapIndex)
    , irradianceMapIndex(other.irradianceMapIndex)
    , prefilteredMapIndex(other.prefilteredMapIndex)
    , brdfLUTIndex(other.brdfLUTIndex)
    , params(other.params)
    , currentType(other.currentType) {
    other.context = nullptr;
    other.materialManager = nullptr;
    other.descriptorManager = nullptr;
    other.textureManager = nullptr;
    other.environmentTextureHandle = {};
    other.irradianceMapHandle = {};
    other.prefilteredMapHandle = {};
    other.brdfLUTHandle = {};
    other.environmentMapIndex = 0;
    other.irradianceMapIndex = 0;
    other.prefilteredMapIndex = 0;
    other.brdfLUTIndex = 0;
    other.params = {};
    other.currentType = Type::Cubemap;
}

EnvironmentMap& EnvironmentMap::operator=(EnvironmentMap&& other) noexcept {
    if (this != &other) {
        cleanup();
        context = other.context;
        materialManager = other.materialManager;
        descriptorManager = other.descriptorManager;
        textureManager = other.textureManager;
        environmentTextureHandle = other.environmentTextureHandle;
        irradianceMapHandle = other.irradianceMapHandle;
        prefilteredMapHandle = other.prefilteredMapHandle;
        brdfLUTHandle = other.brdfLUTHandle;
        environmentMapIndex = other.environmentMapIndex;
        irradianceMapIndex = other.irradianceMapIndex;
        prefilteredMapIndex = other.prefilteredMapIndex;
        brdfLUTIndex = other.brdfLUTIndex;
        params = other.params;
        currentType = other.currentType;

        other.context = nullptr;
        other.materialManager = nullptr;
        other.descriptorManager = nullptr;
        other.textureManager = nullptr;
        other.environmentTextureHandle = {};
        other.irradianceMapHandle = {};
        other.prefilteredMapHandle = {};
        other.brdfLUTHandle = {};
        other.environmentMapIndex = 0;
        other.irradianceMapIndex = 0;
        other.prefilteredMapIndex = 0;
        other.brdfLUTIndex = 0;
        other.params = {};
        other.currentType = Type::Cubemap;
    }
    return *this;
}

void EnvironmentMap::init(VulkanContext* ctx, MaterialManager* matMgr, DescriptorManager* descMgr, TextureManager* texMgr, ShaderLibrary* shaderLib) {
    context = ctx;
    materialManager = matMgr;
    descriptorManager = descMgr;
    textureManager = texMgr;
    shaderLibrary = shaderLib;

    violet::Log::info("Renderer", "EnvironmentMap initialized (resources managed by TextureManager, MaterialManager, DescriptorManager)");
}

void EnvironmentMap::cleanup() {
    // Clear temporary compute resources (must be done first while device is still valid)
    // Order matters: image views → descriptor sets → textures (which own the images)
    tempImageViews.clear();
    tempDescriptorSets.clear();
    tempComputeTextures.clear();

    // Free bindless indices if allocated
    if (descriptorManager) {
        if (environmentMapIndex != 0) {
            descriptorManager->freeBindlessTexture(environmentMapIndex);
            environmentMapIndex = 0;
        }
        if (irradianceMapIndex != 0) {
            descriptorManager->freeBindlessTexture(irradianceMapIndex);
            irradianceMapIndex = 0;
        }
        if (prefilteredMapIndex != 0) {
            descriptorManager->freeBindlessTexture(prefilteredMapIndex);
            prefilteredMapIndex = 0;
        }
        if (brdfLUTIndex != 0) {
            descriptorManager->freeBindlessTexture(brdfLUTIndex);
            brdfLUTIndex = 0;
        }
    }

    // Release texture handles (TextureManager owns the actual textures)
    if (textureManager) {
        if (environmentTextureHandle.isValid()) {
            textureManager->removeTexture(environmentTextureHandle);
            environmentTextureHandle = {};
        }
        if (irradianceMapHandle.isValid()) {
            textureManager->removeTexture(irradianceMapHandle);
            irradianceMapHandle = {};
        }
        if (prefilteredMapHandle.isValid()) {
            textureManager->removeTexture(prefilteredMapHandle);
            prefilteredMapHandle = {};
        }
        if (brdfLUTHandle.isValid()) {
            textureManager->removeTexture(brdfLUTHandle);
            brdfLUTHandle = {};
        }
    }

    context = nullptr;
    materialManager = nullptr;
    descriptorManager = nullptr;
    textureManager = nullptr;
}

void EnvironmentMap::loadHDR(const eastl::string& hdrPath) {
    if (!context || !descriptorManager || !textureManager) {
        violet::Log::error("Renderer", "EnvironmentMap not initialized");
        return;
    }

    eastl::string resolvedPath = FileSystem::resolveRelativePath(hdrPath);
    violet::Log::info("Renderer", "Loading HDR environment map from: {}", resolvedPath.c_str());

    // Generate cubemap from HDR using compute shader (Step 3 implementation)
    const uint32_t cubemapSize = 512;
    generateCubemapFromEquirect(resolvedPath, cubemapSize);

    if (!environmentTextureHandle.isValid()) {
        violet::Log::error("Renderer", "Failed to generate environment cubemap from HDR");
        return;
    }

    // Get texture and register to bindless system
    Texture* envTexture = textureManager->getTexture(environmentTextureHandle);
    if (!envTexture) {
        violet::Log::error("Renderer", "Failed to retrieve environment texture from TextureManager");
        return;
    }

    environmentMapIndex = descriptorManager->allocateBindlessCubemap(envTexture);
    if (environmentMapIndex == 0) {
        violet::Log::error("Renderer", "Failed to allocate bindless cubemap index for environment map");
        return;
    }

    currentType = Type::Cubemap;
    params.enabled = true;

    violet::Log::info("Renderer", "HDR environment map loaded successfully (bindless index: {})", environmentMapIndex);
}

void EnvironmentMap::loadCubemap(const eastl::array<eastl::string, 6>& facePaths) {
    if (!context || !descriptorManager || !textureManager) {
        violet::Log::error("Renderer", "EnvironmentMap not initialized");
        return;
    }

    auto cubemapTexture = ResourceFactory::createCubemapTexture(context, facePaths);
    if (!cubemapTexture) {
        violet::Log::error("Renderer", "Failed to load cubemap from face paths");
        return;
    }

    // Set sampler before adding to TextureManager
    cubemapTexture->setSampler(descriptorManager->getSampler(SamplerType::Cubemap));

    // Add to TextureManager
    environmentTextureHandle = textureManager->addTexture(eastl::move(cubemapTexture));
    if (!environmentTextureHandle.isValid()) {
        violet::Log::error("Renderer", "Failed to add cubemap to TextureManager");
        return;
    }

    // Register to bindless system
    Texture* envTexture = textureManager->getTexture(environmentTextureHandle);
    environmentMapIndex = descriptorManager->allocateBindlessCubemap(envTexture);
    if (environmentMapIndex == 0) {
        violet::Log::error("Renderer", "Failed to allocate bindless cubemap index for cubemap");
        return;
    }

    currentType = Type::Cubemap;
    params.enabled = true;

    violet::Log::info("Renderer", "Environment cubemap loaded successfully (bindless index: {})", environmentMapIndex);
}

void EnvironmentMap::generateIBLMaps() {
    if (!environmentTextureHandle.isValid()) {
        violet::Log::warn("Renderer", "Cannot generate IBL maps: no environment texture loaded");
        return;
    }

    violet::Log::info("Renderer", "Generating IBL maps from environment texture...");

    // Step 5: IBL generation implementation
    // executeSingleTimeCommands already includes waitIdle for each pass
    generateIrradianceMap();
    generatePrefilteredMap();
    generateBRDFLUT();

    violet::Log::info("Renderer", "IBL maps generated successfully");
}

// Texture access methods
Texture* EnvironmentMap::getEnvironmentTexture() const {
    return textureManager ? textureManager->getTexture(environmentTextureHandle) : nullptr;
}

Texture* EnvironmentMap::getIrradianceMap() const {
    return textureManager ? textureManager->getTexture(irradianceMapHandle) : nullptr;
}

Texture* EnvironmentMap::getPrefilteredMap() const {
    return textureManager ? textureManager->getTexture(prefilteredMapHandle) : nullptr;
}

Texture* EnvironmentMap::getBRDFLUT() const {
    return textureManager ? textureManager->getTexture(brdfLUTHandle) : nullptr;
}

// ===== Private Helper Methods =====

void EnvironmentMap::generateCubemapFromEquirect(const eastl::string& hdrPath, uint32_t cubemapSize) {
    violet::Log::info("Renderer", "Generating cubemap from equirectangular HDR (size: {})", cubemapSize);

    // Step 1: Load equirect HDR texture (temporary, 2D)
    // Keep alive to prevent validation errors from descriptor set reuse
    auto equirectTexture = eastl::make_unique<Texture>();
    equirectTexture->loadHDR(context, hdrPath);

    if (!equirectTexture->getImageView()) {
        violet::Log::error("Renderer", "Failed to load equirectangular HDR texture");
        return;
    }

    // Set sampler for the equirect texture (needed for compute shader sampling)
    equirectTexture->setSampler(descriptorManager->getSampler(SamplerType::Default));

    Texture* equirectTexturePtr = equirectTexture.get();  // Get raw pointer before moving
    tempComputeTextures.push_back(eastl::move(equirectTexture));  // Keep alive

    // Step 2: Create output cubemap
    auto cubemap = eastl::make_unique<Texture>();
    cubemap->createEmptyCubemap(
        context,
        cubemapSize,
        vk::Format::eR16G16B16A16Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled
    );

    Texture* cubemapPtr = cubemap.get();  // Get raw pointer before using

    // Step 3: Create compute pipeline
    ComputePipeline pipeline;
    ComputePipelineConfig config;
    config.descriptorSetLayouts.push_back(descriptorManager->getLayout("EquirectToCubemap"));

    vk::PushConstantRange pushConstant;
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t) * 2; // cubemapSize + currentFace
    config.pushConstantRanges.push_back(pushConstant);

auto shader = shaderLibrary->get("equirect_to_cubemap");
    pipeline.init(context, shader, config);

    // Step 4: Allocate descriptor set
    auto descriptorSets = descriptorManager->allocateSets("EquirectToCubemap", 1);
    auto descSet = eastl::make_unique<DescriptorSet>();
    descSet->init(context, descriptorSets);

    // Update descriptor set
    descSet->updateTexture(0, equirectTexturePtr, 0);  // Binding 0: input
    descSet->updateStorageImage(0, cubemapPtr, 1);     // Binding 1: output

    // Keep descriptor set alive to prevent validation errors
    DescriptorSet* descSetPtr = descSet.get();
    tempDescriptorSets.push_back(eastl::move(descSet));

    // Step 5: Execute compute shader
    ResourceFactory::executeSingleTimeCommands(context, [&](vk::CommandBuffer cmd) {
        // Transition cubemap to general layout
        vk::ImageMemoryBarrier barrier;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eGeneral;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = cubemapPtr->getImage();
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 6;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, 0, nullptr, 0, nullptr, 1, &barrier
        );

        // Bind pipeline and descriptor set
        pipeline.bind(cmd);
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            pipeline.getPipelineLayout(),
            0, descSetPtr->getDescriptorSet(0), {}
        );

        // Single dispatch for all 6 faces using Z dimension (ultimate optimization)
        // Z = 0..5 maps to cubemap faces, gl_GlobalInvocationID.z determines face index
        uint32_t workgroupCountX = (cubemapSize + 15) / 16;
        uint32_t workgroupCountY = (cubemapSize + 15) / 16;

        cmd.pushConstants(
            pipeline.getPipelineLayout(),
            vk::ShaderStageFlagBits::eCompute,
            0, sizeof(uint32_t), &cubemapSize
        );

        pipeline.dispatch(cmd, workgroupCountX, workgroupCountY, 6);  // Z=6 for all faces

        // Transition to shader read-only
        vk::ImageMemoryBarrier finalBarrier;
        finalBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        finalBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        finalBarrier.oldLayout = vk::ImageLayout::eGeneral;
        finalBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        finalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        finalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        finalBarrier.image = cubemapPtr->getImage();
        finalBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        finalBarrier.subresourceRange.baseMipLevel = 0;
        finalBarrier.subresourceRange.levelCount = 1;
        finalBarrier.subresourceRange.baseArrayLayer = 0;
        finalBarrier.subresourceRange.layerCount = 6;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, 0, nullptr, 0, nullptr, 1, &finalBarrier
        );
    });

    // Step 6: Set sampler before adding to TextureManager
    cubemapPtr->setSampler(descriptorManager->getSampler(SamplerType::Cubemap));

    // Step 7: Add to TextureManager
    environmentTextureHandle = textureManager->addTexture(eastl::move(cubemap));

    violet::Log::info("Renderer", "Cubemap generated successfully from HDR");
}

void EnvironmentMap::generateIrradianceMap() {
    if (!environmentTextureHandle.isValid()) {
        violet::Log::error("Renderer", "Cannot generate irradiance map: no environment texture");
        return;
    }

    violet::Log::info("Renderer", "Generating irradiance map...");

    const uint32_t irradianceSize = 32;  // Low resolution for diffuse

    // Create output irradiance cubemap
    auto irradiance = eastl::make_unique<Texture>();
    irradiance->createEmptyCubemap(
        context, irradianceSize,
        vk::Format::eR16G16B16A16Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled
    );

    // Create compute pipeline
    ComputePipeline pipeline;
    ComputePipelineConfig config;
    config.descriptorSetLayouts.push_back(descriptorManager->getLayout("IrradianceConvolution"));

    vk::PushConstantRange pushConstant;
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t) * 2;
    config.pushConstantRanges.push_back(pushConstant);

    auto shader = shaderLibrary->get("irradiance_convolution");
    pipeline.init(context, shader, config);

    // Allocate and update descriptor set
    auto descriptorSets = descriptorManager->allocateSets("IrradianceConvolution", 1);
    auto descSet = eastl::make_unique<DescriptorSet>();
    descSet->init(context, descriptorSets);

    Texture* envTex = textureManager->getTexture(environmentTextureHandle);
    descSet->updateTexture(0, envTex, 0);
    descSet->updateStorageImage(0, irradiance.get(), 1);

    // Keep descriptor set alive to prevent validation errors
    DescriptorSet* descSetPtr = descSet.get();
    tempDescriptorSets.push_back(eastl::move(descSet));

    // Execute compute shader
    ResourceFactory::executeSingleTimeCommands(context, [&](vk::CommandBuffer cmd) {
        // Transition to general layout
        vk::ImageMemoryBarrier barrier;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eGeneral;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = irradiance->getImage();
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 6;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, 0, nullptr, 0, nullptr, 1, &barrier
        );

        pipeline.bind(cmd);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline.getPipelineLayout(), 0, descSetPtr->getDescriptorSet(0), {});

        struct { uint32_t size; uint32_t face; } pc;
        pc.size = irradianceSize;
        uint32_t workgroups = (irradianceSize + 15) / 16;

        // Dispatch for each cubemap face (GPU can parallelize these independent writes)
        for (uint32_t face = 0; face < 6; ++face) {
            pc.face = face;
            cmd.pushConstants(pipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
            pipeline.dispatch(cmd, workgroups, workgroups, 1);
            // No barrier needed: each face writes to a different array layer
        }

        // Transition to shader read-only
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eGeneral;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader, {}, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    // Set sampler before adding to TextureManager
    irradiance->setSampler(descriptorManager->getSampler(SamplerType::Cubemap));

    irradianceMapHandle = textureManager->addTexture(eastl::move(irradiance));
    irradianceMapIndex = descriptorManager->allocateBindlessCubemap(textureManager->getTexture(irradianceMapHandle));

    violet::Log::info("Renderer", "Irradiance map generated (bindless cubemap index: {})", irradianceMapIndex);
}

void EnvironmentMap::generatePrefilteredMap() {
    if (!environmentTextureHandle.isValid()) {
        violet::Log::error("Renderer", "Cannot generate prefiltered map: no environment texture");
        return;
    }

    violet::Log::info("Renderer", "Generating prefiltered environment map with mipmaps...");

    const uint32_t prefilteredSize = 128;
    const uint32_t mipLevels = 5;  // 128, 64, 32, 16, 8 (roughness: 0.0, 0.25, 0.5, 0.75, 1.0)

    // Create output cubemap with mip levels
    auto prefiltered = eastl::make_unique<Texture>();
    prefiltered->createEmptyCubemap(
        context, prefilteredSize,
        vk::Format::eR16G16B16A16Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        mipLevels
    );

    // Create compute pipeline
    ComputePipeline pipeline;
    ComputePipelineConfig config;
    config.descriptorSetLayouts.push_back(descriptorManager->getLayout("PrefilterEnvironment"));

    vk::PushConstantRange pushConstant;
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t) * 4;
    config.pushConstantRanges.push_back(pushConstant);

    auto shader = shaderLibrary->get("prefilter_environment");
    pipeline.init(context, shader, config);

    Texture* envTex = textureManager->getTexture(environmentTextureHandle);

    // Process each mip level with different roughness
    ResourceFactory::executeSingleTimeCommands(context, [&](vk::CommandBuffer cmd) {
        // Transition all mip levels to general layout
        vk::ImageMemoryBarrier barrier;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eGeneral;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = prefiltered->getImage();
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 6;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader, {}, 0, nullptr, 0, nullptr, 1, &barrier);

        pipeline.bind(cmd);

        struct { uint32_t size; uint32_t face; float roughness; uint32_t padding; } pc;

        // Generate each mip level with increasing roughness
        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            uint32_t mipSize = prefilteredSize >> mip;
            pc.size = mipSize;
            pc.roughness = static_cast<float>(mip) / static_cast<float>(mipLevels - 1);

            // Create per-mip descriptor set
            auto mipDescriptorSets = descriptorManager->allocateSets("PrefilterEnvironment", 1);
            auto mipDescSet = eastl::make_unique<DescriptorSet>();
            mipDescSet->init(context, mipDescriptorSets);

            // Create per-mip image view for storage
            vk::raii::ImageView mipView = prefiltered->createMipImageView(context, mip);

            // Update descriptor set for this mip level
            eastl::vector<ResourceBindingDesc> bindings;
            bindings.push_back(ResourceBindingDesc::texture(0, envTex));
            bindings.push_back(ResourceBindingDesc::storageImage(1, *mipView));
            descriptorManager->updateSet(mipDescriptorSets[0], bindings);

            // Keep descriptor set and image view alive to prevent validation errors
            DescriptorSet* mipDescSetPtr = mipDescSet.get();
            vk::DescriptorSet descriptorSetHandle = mipDescriptorSets[0];
            tempDescriptorSets.push_back(eastl::move(mipDescSet));
            tempImageViews.push_back(eastl::move(mipView));

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline.getPipelineLayout(), 0, descriptorSetHandle, {});

            uint32_t workgroups = (mipSize + 15) / 16;

            // Dispatch for each cubemap face (GPU can parallelize these independent writes)
            for (uint32_t face = 0; face < 6; ++face) {
                pc.face = face;
                cmd.pushConstants(pipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
                pipeline.dispatch(cmd, workgroups, workgroups, 1);
                // No barrier needed: each face writes to a different array layer
            }

            // Barrier between mip levels (required: mip N may depend on mip N-1)
            if (mip < mipLevels - 1) {
                vk::MemoryBarrier mb;
                mb.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
                mb.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, 1, &mb, 0, nullptr, 0, nullptr);
            }
        }

        // Transition all mip levels to shader read-only
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eGeneral;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader, {}, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    // Set sampler before adding to TextureManager
    prefiltered->setSampler(descriptorManager->getSampler(SamplerType::Cubemap));

    prefilteredMapHandle = textureManager->addTexture(eastl::move(prefiltered));
    prefilteredMapIndex = descriptorManager->allocateBindlessCubemap(textureManager->getTexture(prefilteredMapHandle));

    violet::Log::info("Renderer", "Prefiltered map generated with {} mip levels (bindless cubemap index: {})", mipLevels, prefilteredMapIndex);
}

void EnvironmentMap::generateBRDFLUT() {
    violet::Log::info("Renderer", "Generating BRDF lookup table...");

    const uint32_t lutSize = 512;

    // Create 2D BRDF LUT texture
    auto brdfLUT = eastl::make_unique<Texture>();
    brdfLUT->createEmpty2D(
        context, lutSize, lutSize,
        vk::Format::eR16G16Sfloat,  // RG16F format: scale, bias
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled
    );

    // Create compute pipeline
    ComputePipeline pipeline;
    ComputePipelineConfig config;
    config.descriptorSetLayouts.push_back(descriptorManager->getLayout("BRDFLUT"));

    vk::PushConstantRange pushConstant;
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t);  // LUT size
    config.pushConstantRanges.push_back(pushConstant);

    auto shader = shaderLibrary->get("brdf_lut");
    pipeline.init(context, shader, config);

    // Allocate and update descriptor set
    auto descriptorSets = descriptorManager->allocateSets("BRDFLUT", 1);
    auto descSet = eastl::make_unique<DescriptorSet>();
    descSet->init(context, descriptorSets);

    // Create image view for storage
    vk::raii::ImageView lutView = brdfLUT->createMipImageView(context, 0);

    // Update descriptor set
    eastl::vector<ResourceBindingDesc> bindings;
    bindings.push_back(ResourceBindingDesc::storageImage(0, *lutView));
    descriptorManager->updateSet(descriptorSets[0], bindings);

    // Keep descriptor set and image view alive to prevent validation errors
    DescriptorSet* descSetPtr = descSet.get();
    vk::DescriptorSet descriptorSetHandle = descriptorSets[0];
    tempDescriptorSets.push_back(eastl::move(descSet));
    tempImageViews.push_back(eastl::move(lutView));

    // Execute compute shader
    ResourceFactory::executeSingleTimeCommands(context, [&](vk::CommandBuffer cmd) {
        // Transition to general layout
        vk::ImageMemoryBarrier barrier;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eGeneral;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = brdfLUT->getImage();
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, 0, nullptr, 0, nullptr, 1, &barrier
        );

        pipeline.bind(cmd);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline.getPipelineLayout(), 0, descriptorSetHandle, {});

        // Push constants
        cmd.pushConstants(pipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(uint32_t), &lutSize);

        // Dispatch compute shader
        uint32_t workgroups = (lutSize + 15) / 16;
        pipeline.dispatch(cmd, workgroups, workgroups, 1);

        // Transition to shader read-only
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eGeneral;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, 0, nullptr, 0, nullptr, 1, &barrier
        );
    });

    // Set sampler before adding to TextureManager
    brdfLUT->setSampler(descriptorManager->getSampler(SamplerType::ClampToEdge));

    brdfLUTHandle = textureManager->addTexture(eastl::move(brdfLUT));
    brdfLUTIndex = descriptorManager->allocateBindlessTexture(textureManager->getTexture(brdfLUTHandle));

    violet::Log::info("Renderer", "BRDF LUT generated (bindless index: {})", brdfLUTIndex);
}

} // namespace violet
