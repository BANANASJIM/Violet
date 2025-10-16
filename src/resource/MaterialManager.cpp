#include "MaterialManager.hpp"

#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "resource/Material.hpp"
#include "resource/Texture.hpp"
#include "resource/shader/ShaderLibrary.hpp"
#include "renderer/graph/RenderPass.hpp"
#include "resource/gpu/ResourceFactory.hpp"

#include <EASTL/algorithm.h>

namespace violet {

MaterialManager::~MaterialManager() {
    cleanup();
}

void MaterialManager::init(VulkanContext* ctx, DescriptorManager* descMgr, TextureManager* texMgr, ShaderLibrary* shaderLib, uint32_t framesInFlight) {
    context = ctx;
    descriptorManager = descMgr;
    textureManager = texMgr;
    shaderLibrary = shaderLib;
    maxFramesInFlight = framesInFlight;

    // Reserve space for common use cases
    materials.reserve(32);
    instanceSlots.reserve(256);
    freeInstanceIds.reserve(64);

    violet::Log::info("MaterialManager", "Initialized with {} frames in flight", maxFramesInFlight);
}

void MaterialManager::cleanup() {
    // Clear maps first
    globalMaterialMap.clear();
    namedMaterials.clear();

    // Destroy all material instances
    for (auto& slot : instanceSlots) {
        if (slot.inUse && slot.instance) {
            slot.instance->cleanup();
            slot.instance.reset();
        }
    }
    instanceSlots.clear();
    freeInstanceIds.clear();

    // Destroy all materials
    for (auto& material : materials) {
        if (material) {
            material->cleanup();
        }
    }
    materials.clear();

    violet::Log::info("MaterialManager", "Cleaned up all resources");
}

// === Material Management ===

Material* MaterialManager::createMaterial(const MaterialDesc& desc) {
    auto material = eastl::make_unique<Material>();
    material->create(context);

    // Map descriptorSetLayouts array to PipelineConfig
    PipelineConfig finalConfig = desc.pipelineConfig;

    // Clear any existing descriptor set layouts and rebuild from descriptorSetLayouts array
    // This ensures clean sequential order (Set 0, 1, 2, ...)
    finalConfig.globalDescriptorSetLayout = nullptr;
    finalConfig.materialDescriptorSetLayout = nullptr;
    finalConfig.additionalDescriptorSets.clear();

    // Convert descriptor set layout names to actual layouts
    for (const auto& layoutName : desc.descriptorSetLayouts) {
        if (!descriptorManager->hasLayout(layoutName)) {
            violet::Log::error("MaterialManager", "Descriptor set layout '{}' not found in DescriptorManager", layoutName.c_str());
            return nullptr;
        }
        auto layout = descriptorManager->getLayout(layoutName);
        finalConfig.additionalDescriptorSets.push_back(layout);
    }

    // Validate format information is provided
    if (finalConfig.colorFormats.empty()) {
        violet::Log::error("MaterialManager", "Material '{}' has no color formats specified",
                          desc.name.empty() ? "unnamed" : desc.name.c_str());
        return nullptr;
    }

    // Create graphics pipeline with dynamic rendering
    auto pipeline = eastl::make_unique<GraphicsPipeline>();
    pipeline->init(
        context,
        material.get(),
        desc.vertexShader,
        desc.fragmentShader,
        finalConfig
    );

    material->pipeline = eastl::move(pipeline);

    Material* materialPtr = material.get();
    materials.push_back(eastl::move(material));

    // Register in named materials map if name is provided
    if (!desc.name.empty()) {
        namedMaterials[desc.name] = materialPtr;
    }

    violet::Log::debug("MaterialManager", "Created material '{}' (index {}) with {} descriptor sets",
                      desc.name.empty() ? "unnamed" : desc.name.c_str(), materials.size() - 1, desc.descriptorSetLayouts.size());

    return materialPtr;
}

Material* MaterialManager::getMaterial(size_t index) const {
    if (index >= materials.size()) {
        return nullptr;
    }
    return materials[index].get();
}

Material* MaterialManager::getMaterialByName(const eastl::string& name) const {
    auto it = namedMaterials.find(name);
    if (it != namedMaterials.end()) {
        return it->second;
    }
    violet::Log::error("MaterialManager", "Material '{}' is not found", name.c_str());
    return nullptr;
}

// Predefined material creation shortcuts (using dynamic rendering)

Material* MaterialManager::createPBRBindlessMaterial() {
    // Get shaders from ShaderLibrary
    auto vertShader = shaderLibrary->get("pbr_vert");
    auto fragShader = shaderLibrary->get("pbr_frag");

    if (vertShader.expired() || fragShader.expired()) {
        violet::Log::error("MaterialManager", "Failed to get PBR shaders from ShaderLibrary");
        return nullptr;
    }

    // Get format information for PBR material type
    auto formats = getFormatsForMaterialType(MaterialType::PBR);

    // Configure pipeline for PBR bindless rendering
    PipelineConfig config;
    config.enableDepthTest = true;
    config.enableDepthWrite = true;
    config.colorFormats = formats.colorFormats;
   config.depthFormat = formats.depthFormat;
   config.stencilFormat = formats.stencilFormat;

    // Push constants for PBR: mat4 model (64 bytes) + uint materialID (4 bytes) = 68 bytes
    // Round up to 16-byte alignment = 80 bytes
    // IMPORTANT: Both vertex and fragment shaders access push constants (materialID is used in fragment shader)
    vk::PushConstantRange pushConstantRange;
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 80;  // 68 bytes rounded up to 16-byte alignment
    config.pushConstantRanges.push_back(pushConstantRange);

    // Create material descriptor
    MaterialDesc desc;
    desc.vertexShader = vertShader;
    desc.fragmentShader = fragShader;
    desc.pipelineConfig = config;
    desc.name = "PBRBindless";
    desc.type = MaterialType::PBR;
    desc.renderPass = nullptr;  // Dynamic rendering - no RenderPass needed

    // Bindless rendering requires descriptor sets in a specific order:
    // Set 0: Global (camera, lighting)
    // Set 1: Bindless texture array
    // Set 2: Material data SSBO
    desc.descriptorSetLayouts = {"Global", "Bindless", "MaterialData"};

    Material* material = createMaterial(desc);
    if (!material) {
        violet::Log::error("MaterialManager", "Failed to create PBRBindless material");
        return nullptr;
    }

    violet::Log::info("MaterialManager", "PBRBindless material created successfully");
    return material;
}

Material* MaterialManager::createPostProcessMaterial() {
    // Get shaders from ShaderLibrary
    auto vertShader = shaderLibrary->get("postprocess_vert");
    auto fragShader = shaderLibrary->get("postprocess_frag");

    if (vertShader.expired() || fragShader.expired()) {
        violet::Log::error("MaterialManager", "Failed to get PostProcess shaders from ShaderLibrary");
        return nullptr;
    }

    // Get format information for PostProcess material type
    auto formats = getFormatsForMaterialType(MaterialType::PostProcess);

    // Configure pipeline for post-processing
    PipelineConfig config;
    config.useVertexInput = false;  // Full-screen triangle
    config.enableDepthTest = false;  // Don't test against depth
    config.enableDepthWrite = true;   // Write depth from shader (gl_FragDepth)
    config.colorFormats = formats.colorFormats;
    config.depthFormat = formats.depthFormat;  // Match depth format for writing
    config.stencilFormat = formats.stencilFormat;

    // Push constants for tonemap parameters (ev100, gamma, tonemapMode, padding = 16 bytes)
    // IMPORTANT: Even though only fragment shader uses push constants, we need to include
    // both Vertex and Fragment stages due to Vulkan validation requirements
    vk::PushConstantRange pushConstantRange;
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 16;  // float ev100, float gamma, uint tonemapMode, float padding
    config.pushConstantRanges.push_back(pushConstantRange);

    // Create material descriptor
    MaterialDesc desc;
    desc.vertexShader = vertShader;
    desc.fragmentShader = fragShader;
    desc.pipelineConfig = config;
    desc.name = "PostProcess";
    desc.type = MaterialType::PostProcess;
    desc.renderPass = nullptr;  // Dynamic rendering - no RenderPass needed

    // PostProcess only needs its own descriptor set (Set 0)
    // No Global descriptor set required - shader doesn't use camera/lighting data
    desc.descriptorSetLayouts = {"PostProcess"};

    return createMaterial(desc);
}

Material* MaterialManager::createSkyboxMaterial() {
    // Get shaders from ShaderLibrary
    auto vertShader = shaderLibrary->get("skybox_vert");
    auto fragShader = shaderLibrary->get("skybox_frag");

    if (vertShader.expired() || fragShader.expired()) {
        violet::Log::error("MaterialManager", "Failed to get Skybox shaders from ShaderLibrary");
        return nullptr;
    }

    // Get format information for Skybox material type
    auto formats = getFormatsForMaterialType(MaterialType::Skybox);

    // Configure pipeline for skybox rendering
    PipelineConfig config;
    config.useVertexInput = false;  // Skybox uses procedural geometry
    config.enableDepthTest = true;
    config.enableDepthWrite = false;  // Skybox drawn last with equal depth
    config.depthCompareOp = vk::CompareOp::eLessOrEqual;
    config.cullMode = vk::CullModeFlagBits::eNone;
    config.colorFormats = formats.colorFormats;
    config.depthFormat = formats.depthFormat;
    config.stencilFormat = formats.stencilFormat;

    // Create material descriptor
    MaterialDesc desc;
    desc.vertexShader = vertShader;
    desc.fragmentShader = fragShader;
    desc.pipelineConfig = config;
    desc.name = "Skybox";
    desc.type = MaterialType::Skybox;
    desc.renderPass = nullptr;  // Dynamic rendering - no RenderPass needed

    // Skybox needs descriptor sets for camera and cubemap access:
    // Set 0: Global (camera, view/proj matrices)
    // Set 1: Bindless (cubemap array)
    desc.descriptorSetLayouts = {"Global", "Bindless"};

    return createMaterial(desc);
}

// === MaterialInstance Management ===

uint32_t MaterialManager::createMaterialInstance(const MaterialInstanceDesc& desc) {
    if (!desc.material) {
        violet::Log::error("MaterialManager", "Cannot create material instance - null material provided");
        return 0;
    }

    // Allocate instance ID
    uint32_t instanceId = allocateInstanceId();
    if (instanceId == 0) {
        violet::Log::error("MaterialManager", "Failed to allocate instance ID");
        return 0;
    }

    // Create appropriate instance type using EASTL make_unique
    eastl::unique_ptr<MaterialInstance> instance;
    switch (desc.type) {
        case MaterialType::PBR:
            instance = eastl::make_unique<PBRMaterialInstance>();
            break;
        case MaterialType::Unlit:
            instance = eastl::make_unique<UnlitMaterialInstance>();
            break;
        default:
            violet::Log::error("MaterialManager", "Unsupported material type for instance creation");
            releaseInstanceId(instanceId);
            return 0;
    }

    // Initialize instance
    instance->create(context, desc.material, descriptorManager);

    // Store in slot
    uint32_t index = getInstanceIndex(instanceId);
    instanceSlots[index].instance = eastl::move(instance);

    violet::Log::debug("MaterialManager", "Created material instance {} ({})",
                      instanceId, desc.name.empty() ? "unnamed" : desc.name.c_str());

    return instanceId;
}

void MaterialManager::destroyMaterialInstance(uint32_t instanceId) {
    if (!isValidInstanceId(instanceId)) {
        violet::Log::warn("MaterialManager", "Attempting to destroy invalid instance ID {}", instanceId);
        return;
    }

    uint32_t index = getInstanceIndex(instanceId);
    auto& slot = instanceSlots[index];

    if (slot.instance) {
        slot.instance->cleanup();
        slot.instance.reset();
    }

    releaseInstanceId(instanceId);

    violet::Log::debug("MaterialManager", "Destroyed material instance {}", instanceId);
}

MaterialInstance* MaterialManager::getMaterialInstance(uint32_t instanceId) {
    if (!isValidInstanceId(instanceId)) {
        return nullptr;
    }

    uint32_t index = getInstanceIndex(instanceId);
    return instanceSlots[index].instance.get();
}

const MaterialInstance* MaterialManager::getMaterialInstance(uint32_t instanceId) const {
    if (!isValidInstanceId(instanceId)) {
        return nullptr;
    }

    uint32_t index = getInstanceIndex(instanceId);
    return instanceSlots[index].instance.get();
}

// Batch operations
eastl::vector<uint32_t> MaterialManager::createMaterialInstances(const eastl::vector<MaterialInstanceDesc>& descs) {
    eastl::vector<uint32_t> instanceIds;
    instanceIds.reserve(descs.size());

    for (const auto& desc : descs) {
        uint32_t id = createMaterialInstance(desc);
        if (id != 0) {
            instanceIds.push_back(id);
        }
    }

    return instanceIds;
}

void MaterialManager::destroyMaterialInstances(const eastl::vector<uint32_t>& instanceIds) {
    for (uint32_t id : instanceIds) {
        destroyMaterialInstance(id);
    }
}

// === Global Material Registry ===

void MaterialManager::registerGlobalMaterial(uint32_t globalId, uint32_t instanceId) {
    globalMaterialMap[globalId] = instanceId;
    violet::Log::debug("MaterialManager", "Registered global material {:08x} -> instance {}", globalId, instanceId);
}

MaterialInstance* MaterialManager::getGlobalMaterial(uint32_t globalId) {
    auto it = globalMaterialMap.find(globalId);
    if (it != globalMaterialMap.end()) {
        return getMaterialInstance(it->second);
    }
    return nullptr;
}

const MaterialInstance* MaterialManager::getGlobalMaterial(uint32_t globalId) const {
    auto it = globalMaterialMap.find(globalId);
    if (it != globalMaterialMap.end()) {
        return getMaterialInstance(it->second);
    }
    return nullptr;
}

void MaterialManager::unregisterGlobalMaterial(uint32_t globalId) {
    globalMaterialMap.erase(globalId);
}

void MaterialManager::clearGlobalMaterials() {
    globalMaterialMap.clear();
}

// === Texture Management ===

// === Texture Management (delegated to TextureManager) ===

Texture* MaterialManager::addTexture(eastl::unique_ptr<Texture> texture) {
    if (!textureManager) {
        violet::Log::error("MaterialManager", "TextureManager not initialized");
        return nullptr;
    }
    TextureHandle handle = textureManager->addTexture(eastl::move(texture));
    return textureManager->getTexture(handle);
}

Texture* MaterialManager::getDefaultTexture(DefaultTextureType type) const {
    if (!textureManager) {
        violet::Log::error("MaterialManager", "TextureManager not initialized");
        return nullptr;
    }
    return textureManager->getDefaultTexture(type);
}

// === Statistics ===

MaterialManager::Stats MaterialManager::getStats() const {
    Stats stats;
    stats.materialCount = materials.size();
    stats.instanceCount = instanceSlots.size() - freeInstanceIds.size();
    stats.activeInstanceCount = 0;

    for (const auto& slot : instanceSlots) {
        if (slot.inUse) {
            stats.activeInstanceCount++;
        }
    }

    stats.textureCount = textureManager ? textureManager->getTextureCount() : 0;
    stats.globalMaterialCount = globalMaterialMap.size();

    return stats;
}

// === Helper Methods ===

uint32_t MaterialManager::allocateInstanceId() {
    uint32_t index;

    // Try to reuse a free ID
    if (!freeInstanceIds.empty()) {
        index = freeInstanceIds.back();
        freeInstanceIds.pop_back();
    } else {
        // Allocate new slot
        index = static_cast<uint32_t>(instanceSlots.size());
        instanceSlots.emplace_back();
    }

    auto& slot = instanceSlots[index];
    slot.inUse = true;
    slot.generation++;

    return makeInstanceId(index, slot.generation);
}

void MaterialManager::releaseInstanceId(uint32_t id) {
    uint32_t index = getInstanceIndex(id);
    if (index >= instanceSlots.size()) {
        return;
    }

    auto& slot = instanceSlots[index];
    slot.inUse = false;
    slot.instance.reset();

    // Add to free list for reuse
    freeInstanceIds.push_back(index);
}

bool MaterialManager::isValidInstanceId(uint32_t id) const {
    if (id == 0) {
        return false;
    }

    uint32_t index = getInstanceIndex(id);
    if (index >= instanceSlots.size()) {
        return false;
    }

    const auto& slot = instanceSlots[index];
    return slot.inUse && (getInstanceGeneration(id) == slot.generation);
}

uint32_t MaterialManager::getInstanceIndex(uint32_t id) const {
    // Lower 20 bits for index (supports up to 1M instances)
    return id & 0xFFFFF;
}

uint32_t MaterialManager::getInstanceGeneration(uint32_t id) const {
    // Upper 12 bits for generation (4096 generations)
    return (id >> 20) & 0xFFF;
}

uint32_t MaterialManager::makeInstanceId(uint32_t index, uint32_t generation) const {
    // Combine index and generation into single ID
    return (index & 0xFFFFF) | ((generation & 0xFFF) << 20);
}

// === Format Management (for dynamic rendering) ===

void MaterialManager::setRenderingFormats(vk::Format newSwapchainFormat) {
    // Always ensure hdrFormat is initialized (important for first-time setup)
    hdrFormat = vk::Format::eR16G16B16A16Sfloat;

    if (swapchainFormat != newSwapchainFormat) {
        swapchainFormat = newSwapchainFormat;
        depthFormat = context->findDepthFormat();

        violet::Log::info("MaterialManager", "Updated rendering formats (Swapchain: {}, Depth: {}, HDR: {})",
            vk::to_string(swapchainFormat).c_str(),
            vk::to_string(depthFormat).c_str(),
            vk::to_string(hdrFormat).c_str());
    }
}

MaterialManager::PipelineRenderingFormats MaterialManager::getFormatsForMaterialType(MaterialType type) const {
    PipelineRenderingFormats formats;
    formats.depthFormat = depthFormat;

    switch (type) {
        case MaterialType::PBR:
        case MaterialType::Skybox:
            // HDR offscreen rendering
            formats.colorFormats.push_back(hdrFormat);
            break;

        case MaterialType::PostProcess:
            // Swapchain rendering
            formats.colorFormats.push_back(swapchainFormat);
            break;

        case MaterialType::Unlit:
        case MaterialType::Custom:
        default:
            // Default to HDR format
            formats.colorFormats.push_back(hdrFormat);
            break;
    }

    return formats;
}

} // namespace violet
