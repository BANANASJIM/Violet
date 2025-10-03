#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/string.h>
#include <EASTL/fixed_vector.h>

#include "renderer/GraphicsPipeline.hpp"
#include "renderer/TextureManager.hpp"

namespace violet {

class VulkanContext;
class DescriptorManager;
class Material;
class MaterialInstance;
class Texture;
class TextureManager;
class RenderPass;

// Material types
enum class MaterialType {
    PBR,
    Unlit,
    PostProcess,
    Skybox,
    Custom
};

// Material creation descriptor
struct MaterialDesc {
    eastl::string vertexShader;
    eastl::string fragmentShader;
    eastl::string layoutName;           // Descriptor layout name from DescriptorManager
    PipelineConfig pipelineConfig;
    RenderPass* renderPass = nullptr;
    eastl::string name;                 // Optional material name for debugging
    MaterialType type = MaterialType::Custom;
};

// Material instance creation descriptor
struct MaterialInstanceDesc {
    Material* material = nullptr;
    MaterialType type = MaterialType::PBR;
    eastl::string name;                 // Optional instance name for debugging
};

// Centralized material management system
class MaterialManager {
public:
    MaterialManager() = default;
    ~MaterialManager();

    // Non-copyable, non-movable
    MaterialManager(const MaterialManager&) = delete;
    MaterialManager& operator=(const MaterialManager&) = delete;
    MaterialManager(MaterialManager&&) = delete;
    MaterialManager& operator=(MaterialManager&&) = delete;

    // === Initialization ===
    void init(VulkanContext* ctx, DescriptorManager* descMgr, TextureManager* texMgr, uint32_t maxFramesInFlight);
    void cleanup();

    // === Material Management (using vector for stable storage) ===
    Material* createMaterial(const MaterialDesc& desc);
    Material* getMaterial(size_t index) const;
    size_t getMaterialCount() const { return materials.size(); }

    // Generic factory method with custom pipeline config
    Material* createMaterialWithConfig(
        const eastl::string& vertexShader,
        const eastl::string& fragmentShader,
        const eastl::string& layoutName,
        const PipelineConfig& config,
        RenderPass* renderPass,
        const eastl::string& name = ""
    );

    // Predefined material shortcuts
    Material* createPBRBindlessMaterial(RenderPass* renderPass);
    Material* createUnlitMaterial(RenderPass* renderPass);
    Material* createPostProcessMaterial(RenderPass* renderPass);
    Material* createSkyboxMaterial(RenderPass* renderPass);

    // === MaterialInstance Management (dynamic creation/deletion) ===
    uint32_t createMaterialInstance(const MaterialInstanceDesc& desc);
    void destroyMaterialInstance(uint32_t instanceId);
    MaterialInstance* getMaterialInstance(uint32_t instanceId);
    const MaterialInstance* getMaterialInstance(uint32_t instanceId) const;

    // Batch operations using EASTL fixed_vector for small batches
    eastl::vector<uint32_t> createMaterialInstances(const eastl::vector<MaterialInstanceDesc>& descs);
    void destroyMaterialInstances(const eastl::vector<uint32_t>& instanceIds);

    // === Global Material Registry (for scene loading) ===
    void registerGlobalMaterial(uint32_t globalId, uint32_t instanceId);
    MaterialInstance* getGlobalMaterial(uint32_t globalId);
    const MaterialInstance* getGlobalMaterial(uint32_t globalId) const;
    void unregisterGlobalMaterial(uint32_t globalId);
    void clearGlobalMaterials();

    // === Texture Management (delegated to TextureManager) ===
    Texture* addTexture(eastl::unique_ptr<Texture> texture);
    Texture* getDefaultTexture(DefaultTextureType type) const;

    // === Statistics ===
    struct Stats {
        size_t materialCount = 0;
        size_t instanceCount = 0;
        size_t activeInstanceCount = 0;
        size_t textureCount = 0;
        size_t globalMaterialCount = 0;
    };
    Stats getStats() const;

private:
    // Instance slot for sparse set pattern with EASTL optimizations
    struct InstanceSlot {
        eastl::unique_ptr<MaterialInstance> instance;
        uint32_t generation = 0;  // Generation counter for ID validation
        bool inUse = false;

        InstanceSlot() = default;
        InstanceSlot(InstanceSlot&&) = default;
        InstanceSlot& operator=(InstanceSlot&&) = default;
    };

    // === Storage ===
    // Materials - stable storage using vector
    eastl::vector<eastl::unique_ptr<Material>> materials;

    // MaterialInstances - sparse set pattern for dynamic management
    eastl::vector<InstanceSlot> instanceSlots;
    eastl::vector<uint32_t> freeInstanceIds;  // Free ID pool
    uint32_t nextInstanceId = 1;  // Start from 1, 0 is invalid

    // Global material registry (glTF: fileId << 16 | materialIndex -> instanceId)
    eastl::hash_map<uint32_t, uint32_t> globalMaterialMap;

    // === Dependencies ===
    VulkanContext* context = nullptr;
    DescriptorManager* descriptorManager = nullptr;
    TextureManager* textureManager = nullptr;
    uint32_t maxFramesInFlight = 0;

    // === Helper Methods ===
    uint32_t allocateInstanceId();
    void releaseInstanceId(uint32_t id);
    bool isValidInstanceId(uint32_t id) const;
    uint32_t getInstanceIndex(uint32_t id) const;
    uint32_t getInstanceGeneration(uint32_t id) const;
    uint32_t makeInstanceId(uint32_t index, uint32_t generation) const;
};

} // namespace violet