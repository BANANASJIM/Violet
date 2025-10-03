#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/hash_map.h>
#include <glm/glm.hpp>
#include "ResourceFactory.hpp"

namespace violet {

class VulkanContext;
class UniformBuffer;
class Texture;

// Bindless push constants for PBR rendering
struct BindlessPushConstants {
    glm::mat4 model;
    uint32_t materialID;
    uint32_t padding[3];  // Align to 16 bytes
};

// Sampler types for common use cases
enum class SamplerType {
    Default,        // Linear, Repeat, Anisotropy enabled
    ClampToEdge,    // Linear, ClampToEdge, No anisotropy (for PostProcess)
    Nearest,        // Nearest, Repeat, No anisotropy
    Shadow,         // Linear, ClampToBorder, CompareOp enabled
    Cubemap,        // Linear, ClampToEdge, No anisotropy (for skybox/environment)
    NearestClamp    // Nearest, ClampToEdge, No anisotropy
};

// Declarative sampler configuration
struct SamplerConfig {
    vk::Filter magFilter = vk::Filter::eLinear;
    vk::Filter minFilter = vk::Filter::eLinear;
    vk::SamplerAddressMode addressModeU = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode addressModeV = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode addressModeW = vk::SamplerAddressMode::eRepeat;
    vk::SamplerMipmapMode mipmapMode = vk::SamplerMipmapMode::eLinear;
    float minLod = 0.0f;
    float maxLod = VK_LOD_CLAMP_NONE;
    float mipLodBias = 0.0f;
    bool anisotropyEnable = false;
    float maxAnisotropy = 1.0f;
    vk::BorderColor borderColor = vk::BorderColor::eFloatOpaqueBlack;
    bool compareEnable = false;
    vk::CompareOp compareOp = vk::CompareOp::eNever;

    // Hash for caching
    size_t hash() const;
    bool operator==(const SamplerConfig& other) const;

    // Predefined configurations
    static SamplerConfig getDefault(float maxAnisotropy);
    static SamplerConfig getClampToEdge();
    static SamplerConfig getNearest();
    static SamplerConfig getShadow();
    static SamplerConfig getCubemap();
    static SamplerConfig getNearestClamp();
};

// Descriptor update frequency determines pool allocation strategy
enum class UpdateFrequency {
    PerFrame,      // Updates every frame (camera, time)
    PerPass,       // Updates per render pass (shadow maps, render targets)
    PerMaterial,   // Updates when material changes (material properties, textures)
    Static         // Rarely updates (bindless arrays, global resources)
};

// Declarative binding description
struct BindingDesc {
    uint32_t binding;
    vk::DescriptorType type;
    vk::ShaderStageFlags stages;
    uint32_t count = 1;  // Array size (1 for single descriptor)
};

// Declarative descriptor set layout description
struct DescriptorLayoutDesc {
    eastl::string name;
    eastl::vector<BindingDesc> bindings;
    UpdateFrequency frequency = UpdateFrequency::PerMaterial;
    vk::DescriptorSetLayoutCreateFlags flags = {};
    vk::DescriptorBindingFlags bindingFlags = {};  // For bindless (UPDATE_AFTER_BIND, etc.)
};

// Resource binding for declarative updates
struct ResourceBindingDesc {
    uint32_t binding = 0;
    vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;

    // Resource union (only one should be valid based on type)
    union {
        UniformBuffer* bufferPtr;
        Texture* texturePtr;
        struct {
            vk::ImageView imageView;
            vk::Sampler sampler;
        } imageInfo;
        vk::ImageView storageImageView;
    };

    vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    // Default constructor (required because union has non-trivial member)
    ResourceBindingDesc() : bufferPtr(nullptr) {}

    // Helper constructors for type safety
    static ResourceBindingDesc uniformBuffer(uint32_t binding, UniformBuffer* buffer);
    static ResourceBindingDesc texture(uint32_t binding, Texture* texture);
    static ResourceBindingDesc storageImage(uint32_t binding, vk::ImageView imageView);
    static ResourceBindingDesc sampledImage(uint32_t binding, vk::ImageView imageView, vk::Sampler sampler);
};

// Central descriptor management system with declarative API
class DescriptorManager {
public:
    void init(VulkanContext* context, uint32_t maxFramesInFlight);
    void cleanup();

    // Declarative layout registration
    void registerLayout(const DescriptorLayoutDesc& desc);

    // Allocate descriptor set from registered layout
    vk::DescriptorSet allocateSet(const eastl::string& layoutName, uint32_t frameIndex);

    // Allocate multiple descriptor sets (typically one per frame in flight)
    eastl::vector<vk::DescriptorSet> allocateSets(const eastl::string& layoutName, uint32_t count);

    // Declarative batch update of descriptor set
    void updateSet(vk::DescriptorSet set, const eastl::vector<ResourceBindingDesc>& bindings);

    // Get layout for pipeline creation
    vk::DescriptorSetLayout getLayout(const eastl::string& layoutName) const;

    // Check if layout exists
    bool hasLayout(const eastl::string& layoutName) const;

    // Sampler management - centralized sampler creation and caching
    vk::Sampler getSampler(SamplerType type);
    vk::Sampler getOrCreateSampler(const SamplerConfig& config);

    // Bindless texture management integration
    void initBindless(uint32_t maxTextures);
    uint32_t allocateBindlessTexture(Texture* texture);
    void freeBindlessTexture(uint32_t index);
    vk::DescriptorSet getBindlessSet() const;
    bool isBindlessEnabled() const { return bindlessEnabled; }

    // Material data SSBO management (for bindless architecture)
    // This structure is shared between CPU and GPU (must match shader layout exactly)
    struct MaterialData {
        // Material parameters
        alignas(16) glm::vec4 baseColorFactor{1.0f};
        alignas(4) float metallicFactor{0.0f};
        alignas(4) float roughnessFactor{0.8f};
        alignas(4) float normalScale{1.0f};
        alignas(4) float occlusionStrength{1.0f};
        alignas(16) glm::vec3 emissiveFactor{0.0f};
        alignas(4) float alphaCutoff{0.5f};

        // Texture indices (index into bindless texture array)
        alignas(4) uint32_t baseColorTexIndex{0};
        alignas(4) uint32_t metallicRoughnessTexIndex{0};
        alignas(4) uint32_t normalTexIndex{0};
        alignas(4) uint32_t occlusionTexIndex{0};
        alignas(4) uint32_t emissiveTexIndex{0};
        alignas(4) uint32_t padding[3]{0, 0, 0};  // Align to 16 bytes
    };

    // Initialize material data buffer, returns true on success
    bool initMaterialDataBuffer(uint32_t maxMaterials = 1024);

    // Allocate material data, returns index (0 means failure)
    uint32_t allocateMaterialData(const MaterialData& data);

    // Update material data, returns true on success
    bool updateMaterialData(uint32_t index, const MaterialData& data);

    // Free material data, returns true on success
    bool freeMaterialData(uint32_t index);

    // Get material data descriptor set for binding
    vk::DescriptorSet getMaterialDataSet() const { return materialDataSet; }

    // Check if material data buffer is ready
    bool isMaterialDataEnabled() const { return materialDataEnabled; }

    // Get material data by index (for debugging)
    const MaterialData* getMaterialData(uint32_t index) const;

    // Get maximum material count
    uint32_t getMaxMaterialData() const { return maxMaterialData; }

private:
    struct LayoutInfo {
        vk::DescriptorSetLayout layout;
        UpdateFrequency frequency;
        eastl::vector<vk::DescriptorPoolSize> poolSizes;
        vk::DescriptorSetLayoutCreateFlags createFlags;
    };

    struct PoolInfo {
        vk::DescriptorPool pool;
        uint32_t remainingSets = 0;
        uint32_t maxSets = 0;
    };

    void createPool(UpdateFrequency frequency);
    void growPool(UpdateFrequency frequency);
    vk::DescriptorPool getOrCreatePool(UpdateFrequency frequency);

    VulkanContext* context = nullptr;
    uint32_t maxFrames = 0;

    eastl::hash_map<eastl::string, LayoutInfo> layouts;
    eastl::hash_map<UpdateFrequency, eastl::vector<PoolInfo>> poolsByFrequency;

    // Bindless texture management
    bool bindlessEnabled = false;
    vk::DescriptorSet bindlessSet;
    eastl::vector<Texture*> bindlessTextureSlots;
    eastl::vector<uint32_t> bindlessFreeIndices;
    uint32_t bindlessMaxTextures = 0;

    // Material data SSBO management
    bool materialDataEnabled = false;
    vk::DescriptorSet materialDataSet;

    // Sampler cache - avoid creating duplicate samplers
    eastl::hash_map<size_t, vk::Sampler> samplerCache;  // Hash -> Sampler
    eastl::hash_map<SamplerType, vk::Sampler> predefinedSamplers;  // Type -> Sampler

    // Internal sampler creation
    vk::Sampler createSampler(const SamplerConfig& config);
    BufferResource materialDataBuffer;  // Managed by ResourceFactory
    void* materialDataMapped = nullptr;  // Persistent mapping from VMA
    eastl::vector<MaterialData> materialDataSlots;
    eastl::vector<uint32_t> materialDataFreeIndices;
    uint32_t maxMaterialData = 0;

    // Pool size configuration per frequency
    static constexpr uint32_t POOL_SIZE_PER_FRAME = 10;
    static constexpr uint32_t POOL_SIZE_PER_PASS = 20;
    static constexpr uint32_t POOL_SIZE_PER_MATERIAL = 100;
    static constexpr uint32_t POOL_SIZE_STATIC = 50;
};

} // namespace violet
