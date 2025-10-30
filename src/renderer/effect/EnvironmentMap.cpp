#include "renderer/effect/EnvironmentMap.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/ShaderResourceBinding.hpp"
#include "resource/Texture.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MaterialManager.hpp"
#include "resource/Material.hpp"
#include "resource/TextureManager.hpp"
#include "resource/shader/ShaderLibrary.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/ShaderResources.hpp"
#include "renderer/vulkan/ComputePipeline.hpp"
#include "renderer/graph/RenderGraph.hpp"
#include "renderer/graph/RenderPass.hpp"
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
    , resourceManager(other.resourceManager)
    , renderGraph(other.renderGraph)
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
    other.resourceManager = nullptr;
    other.renderGraph = nullptr;
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
        resourceManager = other.resourceManager;
        renderGraph = other.renderGraph;
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
        other.resourceManager = nullptr;
        other.renderGraph = nullptr;
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

void EnvironmentMap::init(VulkanContext* ctx, ResourceManager* resMgr, RenderGraph* graph) {
    context = ctx;
    resourceManager = resMgr;
    renderGraph = graph;

    violet::Log::info("Renderer", "EnvironmentMap initialized (resources managed by ResourceManager)");
}

void EnvironmentMap::cleanup() {
    // Check if resourceManager is still valid (it may have been cleaned up already)
    if (!resourceManager) {
        context = nullptr;
        return;
    }

    // Clear temporary compute resources (must be done first while device is still valid)
    // Order matters: image views → textures (which own the images)
    tempImageViews.clear();
    tempComputeTextures.clear();

    // Free bindless indices if allocated
    auto& descriptorManager = resourceManager->getDescriptorManager();
    if (environmentMapIndex != 0) {
        descriptorManager.freeBindlessCubemap(environmentMapIndex);  // Cubemap, not 2D texture
        environmentMapIndex = 0;
    }
    if (irradianceMapIndex != 0) {
        descriptorManager.freeBindlessCubemap(irradianceMapIndex);  // Cubemap, not 2D texture
        irradianceMapIndex = 0;
    }
    if (prefilteredMapIndex != 0) {
        descriptorManager.freeBindlessCubemap(prefilteredMapIndex);  // Cubemap, not 2D texture
        prefilteredMapIndex = 0;
    }
    if (brdfLUTIndex != 0) {
        descriptorManager.freeBindlessTexture(brdfLUTIndex);  // 2D texture (correct)
        brdfLUTIndex = 0;
    }

    // Release texture handles (TextureManager owns the actual textures)
    auto* textureManager = resourceManager->getTextureManager();
    if (textureManager) {  // Check if TextureManager is still valid
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
    resourceManager = nullptr;
}

void EnvironmentMap::loadHDR(const eastl::string& hdrPath) {
    if (!context || !resourceManager) {
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
    auto& descriptorManager = resourceManager->getDescriptorManager();
    auto* textureManager = resourceManager->getTextureManager();
    Texture* envTexture = textureManager->getTexture(environmentTextureHandle);
    if (!envTexture) {
        violet::Log::error("Renderer", "Failed to retrieve environment texture from TextureManager");
        return;
    }

    environmentMapIndex = descriptorManager.allocateBindlessCubemap(envTexture);
    if (environmentMapIndex == 0) {
        violet::Log::error("Renderer", "Failed to allocate bindless cubemap index for environment map");
        return;
    }

    currentType = Type::Cubemap;
    params.enabled = true;

    violet::Log::info("Renderer", "HDR environment map loaded successfully (bindless index: {})", environmentMapIndex);
}

void EnvironmentMap::loadCubemap(const eastl::array<eastl::string, 6>& facePaths) {
    auto& descriptorManager = resourceManager->getDescriptorManager();
    auto* textureManager = resourceManager->getTextureManager();
    if (!context || !resourceManager) {
        violet::Log::error("Renderer", "EnvironmentMap not initialized");
        return;
    }

    auto cubemapTexture = ResourceFactory::createCubemapTexture(context, facePaths);
    if (!cubemapTexture) {
        violet::Log::error("Renderer", "Failed to load cubemap from face paths");
        return;
    }

    // Set sampler before adding to TextureManager
    cubemapTexture->setSampler(descriptorManager.getSamplerManager().getSampler(SamplerType::Cubemap));

    // Add to TextureManager
    environmentTextureHandle = textureManager->addTexture(eastl::move(cubemapTexture));
    if (!environmentTextureHandle.isValid()) {
        violet::Log::error("Renderer", "Failed to add cubemap to TextureManager");
        return;
    }

    // Register to bindless system
    Texture* envTexture = textureManager->getTexture(environmentTextureHandle);
    environmentMapIndex = descriptorManager.allocateBindlessCubemap(envTexture);
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
    return resourceManager ? resourceManager->getTextureManager()->getTexture(environmentTextureHandle) : nullptr;
}

Texture* EnvironmentMap::getIrradianceMap() const {
    return resourceManager ? resourceManager->getTextureManager()->getTexture(irradianceMapHandle) : nullptr;
}

Texture* EnvironmentMap::getPrefilteredMap() const {
    return resourceManager ? resourceManager->getTextureManager()->getTexture(prefilteredMapHandle) : nullptr;
}

Texture* EnvironmentMap::getBRDFLUT() const {
    return resourceManager ? resourceManager->getTextureManager()->getTexture(brdfLUTHandle) : nullptr;
}

// ===== Private Helper Methods =====

void EnvironmentMap::generateCubemapFromEquirect(const eastl::string& hdrPath, uint32_t cubemapSize) {
    auto& descriptorManager = resourceManager->getDescriptorManager();
    auto* textureManager = resourceManager->getTextureManager();
    auto* shaderLibrary = resourceManager->getShaderLibrary();
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
    equirectTexture->setSampler(descriptorManager.getSamplerManager().getSampler(SamplerType::Default));

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

    // Step 3: Get shader first to access reflection data
    auto shader = shaderLibrary->get("equirect_to_cubemap_comp").lock();
    if (!shader) {
        violet::Log::error("Renderer", "Failed to get equirect_to_cubemap_comp shader");
        return;
    }

    // Create compute pipeline using shader reflection
    ComputePipeline pipeline;
    ComputePipelineConfig config;

    // Get descriptor layout from shader reflection
    const auto& layoutHandlesForPipeline = shader->getDescriptorLayoutHandles();
    if (!layoutHandlesForPipeline.empty()) {
        config.descriptorSetLayouts.push_back(descriptorManager.getLayout(layoutHandlesForPipeline[0]));
    }

    vk::PushConstantRange pushConstant;
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t) * 2; // cubemapSize + currentFace
    config.pushConstantRanges.push_back(pushConstant);

    pipeline.init(context, &descriptorManager, shader, config);

    // Step 4: Create ShaderResourceBinding for this compute task
    ShaderResourceBinding binding;
    binding.init(&pipeline, 0);  // Use set 0

    // Create 2D array image view for compute shader (shader uses RWTexture2DArray)
    // Cubemap images can be accessed as 2D arrays with 6 layers for compute shaders
    vk::ImageViewCreateInfo arrayViewInfo;
    arrayViewInfo.image = cubemapPtr->getImage();
    arrayViewInfo.viewType = vk::ImageViewType::e2DArray;  // 2D array, not cube
    arrayViewInfo.format = cubemapPtr->getFormat();
    arrayViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    arrayViewInfo.subresourceRange.baseMipLevel = 0;
    arrayViewInfo.subresourceRange.levelCount = 1;
    arrayViewInfo.subresourceRange.baseArrayLayer = 0;
    arrayViewInfo.subresourceRange.layerCount = 6;  // 6 faces
    vk::raii::ImageView cubemap2DArrayView(context->getDeviceRAII(), arrayViewInfo);

    // Bind resources to ShaderResourceBinding (separate sampler/texture bindings)
    vk::Sampler sampler = descriptorManager.getSamplerManager().getSampler(SamplerType::Default);
    binding.bindSampledImage("equirectangularMap", equirectTexturePtr->getImageView());
    binding.bindStorageImage("outputCubemap", *cubemap2DArrayView);
    binding.bindSampler("texSampler", sampler);

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

        // Bind pipeline and resources
        pipeline.bind(cmd);
        descriptorManager.bindResources(cmd, binding, 0);  // Frame 0 for single-time commands

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
    cubemapPtr->setSampler(descriptorManager.getSamplerManager().getSampler(SamplerType::Cubemap));

    // Step 7: Add to TextureManager
    environmentTextureHandle = textureManager->addTexture(eastl::move(cubemap));

    violet::Log::info("Renderer", "Cubemap generated successfully from HDR");
}

void EnvironmentMap::generateIrradianceMap() {
    auto& descriptorManager = resourceManager->getDescriptorManager();
    auto* textureManager = resourceManager->getTextureManager();
    auto* shaderLibrary = resourceManager->getShaderLibrary();
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

    // Get shader first to access reflection data
    auto shader = shaderLibrary->get("irradiance_convolution_comp").lock();
    if (!shader) {
        violet::Log::error("Renderer", "Failed to get irradiance_convolution_comp shader");
        return;
    }

    // Create compute pipeline using shader reflection
    ComputePipeline pipeline;
    ComputePipelineConfig config;

    // Get descriptor layout from shader reflection
    const auto& layoutHandlesForPipeline = shader->getDescriptorLayoutHandles();
    if (!layoutHandlesForPipeline.empty()) {
        config.descriptorSetLayouts.push_back(descriptorManager.getLayout(layoutHandlesForPipeline[0]));
    }

    vk::PushConstantRange pushConstant;
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t);
    config.pushConstantRanges.push_back(pushConstant);

    pipeline.init(context, &descriptorManager, shader, config);

    // Create ShaderResourceBinding for this compute task
    ShaderResourceBinding binding;
    binding.init(&pipeline, 0);  // Use set 0

    // Create 2D array image view for compute shader (shader uses RWTexture2DArray)
    vk::ImageViewCreateInfo irradianceArrayViewInfo;
    irradianceArrayViewInfo.image = irradiance->getImage();
    irradianceArrayViewInfo.viewType = vk::ImageViewType::e2DArray;
    irradianceArrayViewInfo.format = irradiance->getFormat();
    irradianceArrayViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    irradianceArrayViewInfo.subresourceRange.baseMipLevel = 0;
    irradianceArrayViewInfo.subresourceRange.levelCount = 1;
    irradianceArrayViewInfo.subresourceRange.baseArrayLayer = 0;
    irradianceArrayViewInfo.subresourceRange.layerCount = 6;
    vk::raii::ImageView irradiance2DArrayView(context->getDeviceRAII(), irradianceArrayViewInfo);

    // Bind resources to ShaderResourceBinding
    Texture* envTex = textureManager->getTexture(environmentTextureHandle);
    vk::Sampler sampler = descriptorManager.getSamplerManager().getSampler(SamplerType::Cubemap);
    binding.bindSampledImage("environmentMap", envTex->getImageView());  // Separate sampler binding
    binding.bindStorageImage("irradianceMap", *irradiance2DArrayView);
    binding.bindSampler("texSampler", sampler);

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
        descriptorManager.bindResources(cmd, binding, 0);  // Frame 0 for single-time commands

        // Single dispatch for all 6 faces using Z dimension
        // Z = 0..5 maps to cubemap faces, gl_GlobalInvocationID.z determines face index
        uint32_t workgroups = (irradianceSize + 15) / 16;

        cmd.pushConstants(
            pipeline.getPipelineLayout(),
            vk::ShaderStageFlagBits::eCompute,
            0,
            sizeof(uint32_t),
            &irradianceSize
        );

        pipeline.dispatch(cmd, workgroups, workgroups, 6);  // Z=6 for all faces
        // No barrier needed: each face writes to a different array layer

        // Transition to shader read-only
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eGeneral;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader, {}, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    // Set sampler before adding to TextureManager
    irradiance->setSampler(descriptorManager.getSamplerManager().getSampler(SamplerType::Cubemap));

    irradianceMapHandle = textureManager->addTexture(eastl::move(irradiance));
    irradianceMapIndex = descriptorManager.allocateBindlessCubemap(textureManager->getTexture(irradianceMapHandle));

    violet::Log::info("Renderer", "Irradiance map generated (bindless cubemap index: {})", irradianceMapIndex);
}

void EnvironmentMap::generatePrefilteredMap() {
    auto& descriptorManager = resourceManager->getDescriptorManager();
    auto* textureManager = resourceManager->getTextureManager();
    auto* shaderLibrary = resourceManager->getShaderLibrary();
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

    // Get shader first to access reflection data
    auto shader = shaderLibrary->get("prefilter_environment_comp").lock();
    if (!shader) {
        violet::Log::error("Renderer", "Failed to get prefilter_environment_comp shader");
        return;
    }

    // Create compute pipeline using shader reflection
    ComputePipeline pipeline;
    ComputePipelineConfig config;

    // Get descriptor layout from shader reflection
    const auto& layoutHandlesForPipeline = shader->getDescriptorLayoutHandles();
    if (!layoutHandlesForPipeline.empty()) {
        config.descriptorSetLayouts.push_back(descriptorManager.getLayout(layoutHandlesForPipeline[0]));
    }

    vk::PushConstantRange pushConstant;
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t) * 4;
    config.pushConstantRanges.push_back(pushConstant);

    pipeline.init(context, &descriptorManager, shader, config);

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

            // Create ShaderResourceBinding for this mip level
            ShaderResourceBinding mipBinding;
            mipBinding.init(&pipeline, 0);  // Use set 0

            // Create per-mip 2D array image view for compute shader (shader uses RWTexture2DArray)
            vk::ImageViewCreateInfo mipArrayViewInfo;
            mipArrayViewInfo.image = prefiltered->getImage();
            mipArrayViewInfo.viewType = vk::ImageViewType::e2DArray;  // 2D array, not cube
            mipArrayViewInfo.format = prefiltered->getFormat();
            mipArrayViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            mipArrayViewInfo.subresourceRange.baseMipLevel = mip;
            mipArrayViewInfo.subresourceRange.levelCount = 1;
            mipArrayViewInfo.subresourceRange.baseArrayLayer = 0;
            mipArrayViewInfo.subresourceRange.layerCount = 6;
            vk::raii::ImageView mipView(context->getDeviceRAII(), mipArrayViewInfo);

            // Bind resources to ShaderResourceBinding
            vk::Sampler sampler = descriptorManager.getSamplerManager().getSampler(SamplerType::Cubemap);
            mipBinding.bindSampledImage("environmentMap", envTex->getImageView());  // Separate sampler binding
            mipBinding.bindStorageImage("prefilteredMap", *mipView);
            mipBinding.bindSampler("texSampler", sampler);

            // Keep image view alive to prevent validation errors
            tempImageViews.push_back(eastl::move(mipView));

            // Bind resources
            descriptorManager.bindResources(cmd, mipBinding, 0);  // Frame 0 for single-time commands

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
    prefiltered->setSampler(descriptorManager.getSamplerManager().getSampler(SamplerType::Cubemap));

    prefilteredMapHandle = textureManager->addTexture(eastl::move(prefiltered));
    prefilteredMapIndex = descriptorManager.allocateBindlessCubemap(textureManager->getTexture(prefilteredMapHandle));

    violet::Log::info("Renderer", "Prefiltered map generated with {} mip levels (bindless cubemap index: {})", mipLevels, prefilteredMapIndex);
}

void EnvironmentMap::generateBRDFLUT() {
    auto& descriptorManager = resourceManager->getDescriptorManager();
    auto* textureManager = resourceManager->getTextureManager();
    auto* shaderLibrary = resourceManager->getShaderLibrary();
    violet::Log::info("Renderer", "Generating BRDF lookup table...");

    const uint32_t lutSize = 512;

    // Create 2D BRDF LUT texture
    auto brdfLUT = eastl::make_unique<Texture>();
    brdfLUT->createEmpty2D(
        context, lutSize, lutSize,
        vk::Format::eR16G16Sfloat,  // RG16F format: scale, bias
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled
    );

    // Get shader first to access reflection data
    auto shader = shaderLibrary->get("brdf_lut_comp").lock();
    if (!shader) {
        violet::Log::error("Renderer", "Failed to get brdf_lut_comp shader");
        return;
    }

    // Create compute pipeline using shader reflection
    ComputePipeline pipeline;
    ComputePipelineConfig config;

    // Get descriptor layout from shader reflection
    const auto& layoutHandlesForPipeline = shader->getDescriptorLayoutHandles();
    if (!layoutHandlesForPipeline.empty()) {
        config.descriptorSetLayouts.push_back(descriptorManager.getLayout(layoutHandlesForPipeline[0]));
    }

    vk::PushConstantRange pushConstant;
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t);  // LUT size
    config.pushConstantRanges.push_back(pushConstant);

    pipeline.init(context, &descriptorManager, shader, config);

    // Allocate and update descriptor set using reflection-based API
    const auto& layoutHandles = shader->getDescriptorLayoutHandles();
    if (layoutHandles.empty()) {
        violet::Log::error("Renderer", "No descriptor layouts found in brdf_lut shader");
        return;
    }

    vk::DescriptorSet descSet = descriptorManager.allocateSet(layoutHandles[0]);

    // Create image view for storage
    vk::raii::ImageView lutView = brdfLUT->createMipImageView(context, 0);

    // Update descriptor set using reflection to get actual binding number
    auto* reflection = shader->getShaderReflection();
    auto* brdfLUTRes = reflection->findResource("brdfLUT");

    if (!brdfLUTRes) {
        violet::Log::error("Renderer", "Failed to find brdfLUT resource via reflection");
        return;
    }

    descriptorManager.bindStorageImage(descSet, brdfLUTRes->binding, *lutView);

    // Keep image view alive to prevent validation errors
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
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline.getPipelineLayout(), 0, descSet, {});

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
    brdfLUT->setSampler(descriptorManager.getSamplerManager().getSampler(SamplerType::ClampToEdge));

    brdfLUTHandle = textureManager->addTexture(eastl::move(brdfLUT));
    brdfLUTIndex = descriptorManager.allocateBindlessTexture(textureManager->getTexture(brdfLUTHandle));

    violet::Log::info("Renderer", "BRDF LUT generated (bindless index: {})", brdfLUTIndex);
}

void EnvironmentMap::addToRenderGraph() {
    auto* materialManager = resourceManager->getMaterialManager();
    if (!renderGraph || !params.enabled || !materialManager) {
        return;
    }

    // Get Skybox material from MaterialManager
    auto* skyboxMaterial = materialManager->getMaterialByName("Skybox");
    if (!skyboxMaterial) {
        // Create Skybox material if it doesn't exist
        skyboxMaterial = materialManager->createSkyboxMaterial();
        if (!skyboxMaterial) {
            violet::Log::error("Renderer", "Failed to create Skybox material");
            return;
        }
    }

    // Should be merged with main pass
    renderGraph->addPass("Skybox", [this, skyboxMaterial](RenderGraph::PassBuilder& b, RenderPass& p) {
        // Skybox reads HDR buffer and depth, writes to HDR buffer
        b.read("depth", ResourceUsage::DepthAttachment);
        b.write("hdr", ResourceUsage::ColorAttachment);

        b.execute([this, skyboxMaterial](vk::CommandBuffer cmd, uint32_t frame) {
            if (!skyboxMaterial || !skyboxMaterial->getPipeline()) {
                return;
            }

            // Bind Skybox pipeline
            skyboxMaterial->getPipeline()->bind(cmd);

            // Note: Global descriptor set (set 0) and bindless set (set 1) are already bound
            // by the Main pass, so we don't need to bind them again

            // Draw fullscreen triangle (no vertex buffer needed)
            cmd.draw(3, 1, 0, 0);
        });
    });
}

} // namespace violet
