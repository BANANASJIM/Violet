#include "MaterialManager.hpp"

#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/DescriptorManager.hpp"
#include "renderer/Material.hpp"
#include "renderer/Texture.hpp"
#include "renderer/RenderPass.hpp"
#include "renderer/ResourceFactory.hpp"

#include <EASTL/algorithm.h>

namespace violet {

MaterialManager::~MaterialManager() {
    cleanup();
}

void MaterialManager::init(VulkanContext* ctx, DescriptorManager* descMgr, uint32_t framesInFlight) {
    context = ctx;
    descriptorManager = descMgr;
    maxFramesInFlight = framesInFlight;

    // Reserve space for common use cases
    materials.reserve(32);
    instanceSlots.reserve(256);
    freeInstanceIds.reserve(64);
    textures.reserve(128);

    violet::Log::info("MaterialManager", "Initialized with {} frames in flight", maxFramesInFlight);
}

void MaterialManager::cleanup() {
    // Clear global material map first
    globalMaterialMap.clear();

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

    // Clear textures (they handle their own cleanup in destructors)
    textures.clear();

    // Clear default texture pointers
    defaultTextures = {};
    defaultResourcesCreated = false;

    violet::Log::info("MaterialManager", "Cleaned up all resources");
}

// === Material Management ===

Material* MaterialManager::createMaterial(const MaterialDesc& desc) {
    auto material = eastl::make_unique<Material>();
    material->create(context);

    // Get material descriptor set layout from DescriptorManager
    PipelineConfig finalConfig = desc.pipelineConfig;
    if (!desc.layoutName.empty() && descriptorManager->hasLayout(desc.layoutName)) {
        finalConfig.materialDescriptorSetLayout = descriptorManager->getLayout(desc.layoutName);
    }

    // Set global descriptor set layout from DescriptorManager
    finalConfig.globalDescriptorSetLayout = descriptorManager->getLayout("Global");
    if (!finalConfig.globalDescriptorSetLayout) {
        violet::Log::error("MaterialManager", "Failed to get 'Global' layout from DescriptorManager");
        return nullptr;
    }

    // Create graphics pipeline
    auto pipeline = eastl::make_unique<GraphicsPipeline>();
    RenderPass* renderPass = desc.renderPass;
    if (!renderPass) {
        violet::Log::error("MaterialManager", "RenderPass is required for material creation");
        return nullptr;
    }

    pipeline->init(
        context,
        renderPass,
        material.get(),
        desc.vertexShader,
        desc.fragmentShader,
        finalConfig
    );

    material->pipeline = pipeline.release();

    Material* materialPtr = material.get();
    materials.push_back(eastl::move(material));

    violet::Log::debug("MaterialManager", "Created material '{}' (index {})",
                      desc.name.empty() ? "unnamed" : desc.name.c_str(), materials.size() - 1);

    return materialPtr;
}

Material* MaterialManager::getMaterial(size_t index) const {
    if (index >= materials.size()) {
        return nullptr;
    }
    return materials[index].get();
}

// Generic factory method with custom pipeline config
Material* MaterialManager::createMaterialWithConfig(
    const eastl::string& vertexShader,
    const eastl::string& fragmentShader,
    const eastl::string& layoutName,
    const PipelineConfig& config,
    RenderPass* renderPass,
    const eastl::string& name) {

    MaterialDesc desc{
        .vertexShader = vertexShader,
        .fragmentShader = fragmentShader,
        .layoutName = layoutName,
        .pipelineConfig = config,
        .renderPass = renderPass,
        .name = name.empty() ? "CustomMaterial" : name,
        .type = MaterialType::Custom
    };

    return createMaterial(desc);
}

// Predefined material creation shortcuts

Material* MaterialManager::createPBRBindlessMaterial(RenderPass* renderPass) {
    PipelineConfig bindlessConfig;
    bindlessConfig.pushConstantRanges.push_back(vk::PushConstantRange{
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        sizeof(BindlessPushConstants)
    });

    // Add bindless descriptor set layouts (set 1: bindless textures, set 2: material data SSBO)
    bindlessConfig.additionalDescriptorSets.push_back(descriptorManager->getLayout("Bindless"));
    bindlessConfig.additionalDescriptorSets.push_back(descriptorManager->getLayout("MaterialData"));

    return createMaterialWithConfig(
        FileSystem::resolveRelativePath("build/shaders/pbr_bindless.vert.spv"),
        FileSystem::resolveRelativePath("build/shaders/pbr_bindless.frag.spv"),
        "",  // No traditional material layout needed for bindless
        bindlessConfig,
        renderPass,
        "PBR_Bindless"
    );
}

Material* MaterialManager::createUnlitMaterial(RenderPass* renderPass) {
    return createMaterialWithConfig(
        FileSystem::resolveRelativePath("build/shaders/unlit.vert.spv"),
        FileSystem::resolveRelativePath("build/shaders/unlit.frag.spv"),
        "UnlitMaterial",
        {},  // Default pipeline config
        renderPass,
        "Unlit"
    );
}

Material* MaterialManager::createPostProcessMaterial(RenderPass* renderPass) {
    PipelineConfig postProcessConfig;
    postProcessConfig.cullMode = vk::CullModeFlagBits::eNone;
    postProcessConfig.enableDepthTest = true;
    postProcessConfig.enableDepthWrite = true;
    postProcessConfig.depthCompareOp = vk::CompareOp::eAlways;
    postProcessConfig.useVertexInput = false;

    return createMaterialWithConfig(
        FileSystem::resolveRelativePath("build/shaders/postprocess.vert.spv"),
        FileSystem::resolveRelativePath("build/shaders/postprocess.frag.spv"),
        "PostProcess",
        postProcessConfig,
        renderPass,
        "PostProcess"
    );
}

Material* MaterialManager::createSkyboxMaterial(RenderPass* renderPass) {
    PipelineConfig skyboxConfig;
    skyboxConfig.enableDepthTest = false;
    skyboxConfig.enableDepthWrite = false;
    skyboxConfig.cullMode = vk::CullModeFlagBits::eFront;  // Cull front faces for inside view
    skyboxConfig.useVertexInput = false;  // Skybox generates vertices procedurally

    return createMaterialWithConfig(
        FileSystem::resolveRelativePath("build/shaders/skybox.vert.spv"),
        FileSystem::resolveRelativePath("build/shaders/skybox.frag.spv"),
        "Global",  // Skybox uses global descriptor set for cubemap
        skyboxConfig,
        renderPass,
        "Skybox"
    );
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

Texture* MaterialManager::addTexture(eastl::unique_ptr<Texture> texture) {
    if (!texture) {
        return nullptr;
    }

    Texture* texturePtr = texture.get();
    textures.push_back(eastl::move(texture));
    return texturePtr;
}

Texture* MaterialManager::getDefaultTexture(DefaultTextureType type) const {
    switch (type) {
        case DefaultTextureType::White:
            return defaultTextures.white;
        case DefaultTextureType::Black:
            return defaultTextures.black;
        case DefaultTextureType::Normal:
            return defaultTextures.normal;
        case DefaultTextureType::MetallicRoughness:
            return defaultTextures.metallicRoughness;
        default:
            return nullptr;
    }
}

// === Default Resources ===

void MaterialManager::createDefaultResources() {
    if (defaultResourcesCreated) {
        return;
    }

    createDefaultWhiteTexture();
    createDefaultBlackTexture();
    createDefaultNormalTexture();
    createDefaultMetallicRoughnessTexture();

    // Register default textures in bindless array if enabled
    if (descriptorManager->isBindlessEnabled()) {
        if (defaultTextures.white) {
            uint32_t whiteTexIndex = descriptorManager->allocateBindlessTexture(defaultTextures.white);
            violet::Log::info("MaterialManager", "Registered default white texture at bindless index {}", whiteTexIndex);
        }
    }

    defaultResourcesCreated = true;
    violet::Log::info("MaterialManager", "Created default resources");
}

void MaterialManager::createDefaultWhiteTexture() {
    const uint8_t white[] = {255, 255, 255, 255};
    auto texture = eastl::make_unique<Texture>();
    texture->loadFromMemory(context, white, sizeof(white), 1, 1, 4, false);
    texture->setSampler(descriptorManager->getSampler(SamplerType::Default));
    defaultTextures.white = addTexture(eastl::move(texture));
}

void MaterialManager::createDefaultBlackTexture() {
    const uint8_t black[] = {0, 0, 0, 255};
    auto texture = eastl::make_unique<Texture>();
    texture->loadFromMemory(context, black, sizeof(black), 1, 1, 4, false);
    texture->setSampler(descriptorManager->getSampler(SamplerType::Default));
    defaultTextures.black = addTexture(eastl::move(texture));
}

void MaterialManager::createDefaultNormalTexture() {
    const uint8_t normal[] = {128, 128, 255, 255};  // R=0.5, G=0.5, B=1.0
    auto texture = eastl::make_unique<Texture>();
    texture->loadFromMemory(context, normal, sizeof(normal), 1, 1, 4, false);
    texture->setSampler(descriptorManager->getSampler(SamplerType::Default));
    defaultTextures.normal = addTexture(eastl::move(texture));
}

void MaterialManager::createDefaultMetallicRoughnessTexture() {
    const uint8_t metallicRoughness[] = {255, 128, 0, 255};  // R=1.0 (roughness), G=0.5 (metallic)
    auto texture = eastl::make_unique<Texture>();
    texture->loadFromMemory(context, metallicRoughness, sizeof(metallicRoughness), 1, 1, 4, false);
    texture->setSampler(descriptorManager->getSampler(SamplerType::Default));
    defaultTextures.metallicRoughness = addTexture(eastl::move(texture));
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

    stats.textureCount = textures.size();
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

} // namespace violet