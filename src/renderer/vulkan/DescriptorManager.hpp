#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/shared_ptr.h>
#include <glm/glm.hpp>
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/shader/ShaderReflection.hpp"
#include "core/Log.hpp"

namespace violet {

class VulkanContext;
class UniformBuffer;
class Texture;
class FieldProxy;
class UniformProxy;
class UniformHandle;

// Type alias for descriptor layout handle (hash-based ID)
using LayoutHandle = uint32_t;

// Type alias for push constant handle (hash-based ID)
using PushConstantHandle = uint32_t;

// Type alias for pipeline layout cache handle (hash-based ID for layout combination)
using PipelineLayoutCacheHandle = uint32_t;

//todo remove
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
    vk::DescriptorBindingFlags flags = {};  // Bindless需要特殊flags
};

// Declarative descriptor set layout description
struct DescriptorLayoutDesc {
    eastl::string name;  // For debugging only, not used in hash
    eastl::vector<BindingDesc> bindings;
    UpdateFrequency frequency = UpdateFrequency::PerMaterial;
    vk::DescriptorSetLayoutCreateFlags flags = {};
    vk::DescriptorBindingFlags bindingFlags = {};  //todo 移除，已改用per-binding flags
    bool isBindless = false;  // Bindless需要特殊flags

    // Compute hash based on bindings, frequency, and flags
    LayoutHandle hash() const;
};

// Declarative push constant description
struct PushConstantDesc {
    eastl::vector<vk::PushConstantRange> ranges;

    // Compute hash based on ranges
    PushConstantHandle hash() const;
};

//todo remove
// Resource binding for declarative updates
struct ResourceBindingDesc {
    uint32_t binding = 0;
    vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;

    // Flag to distinguish between Texture* and raw ImageView/Sampler
    // Required due to union aliasing issues
    bool usesRawImageView = false;

    // Resource union (only one should be valid based on type)
    union {
        UniformBuffer* bufferPtr;
        Texture* texturePtr;
        struct {
            vk::ImageView imageView;
            vk::Sampler sampler;
        } imageInfo;
        vk::ImageView storageImageView;
        struct {
            vk::Buffer buffer;
            vk::DeviceSize offset;
            vk::DeviceSize range;
        } storageBufferInfo;
    };

    vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    // Default constructor (required because union has non-trivial member)
    ResourceBindingDesc() : bufferPtr(nullptr) {}

    // Helper constructors for type safety
    static ResourceBindingDesc uniformBuffer(uint32_t binding, UniformBuffer* buffer);
    static ResourceBindingDesc storageBuffer(uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range);
    static ResourceBindingDesc texture(uint32_t binding, Texture* texture);
    static ResourceBindingDesc storageImage(uint32_t binding, vk::ImageView imageView);
    static ResourceBindingDesc sampledImage(uint32_t binding, vk::ImageView imageView, vk::Sampler sampler);
};

// ===== Reflection-Based Uniform Management (Modern API) =====

// Proxy class for type-safe field updates
class FieldProxy {
public:
    FieldProxy(void* bufferData, uint32_t offset, uint32_t size, const eastl::string& fieldName)
        : bufferData(bufferData), offset(offset), size(size), fieldName(fieldName) {}

    // Type-safe assignment operator with trivially copyable constraint
    template<typename T>
    requires std::is_trivially_copyable_v<T>
    FieldProxy& operator=(const T& value) noexcept {
        if (sizeof(T) != size) {
            violet::Log::error("DescriptorManager",
                "Size mismatch for field '{}': expected {} bytes, got {} bytes",
                fieldName.c_str(), size, sizeof(T));
            return *this;
        }

        if (bufferData) {
            memcpy(static_cast<uint8_t*>(bufferData) + offset, &value, sizeof(T));
        }
        return *this;
    }

private:
    void* bufferData;
    uint32_t offset;
    uint32_t size;
    eastl::string fieldName;
};

// Handle class returned to users for uniform access
class UniformHandle {
public:
    UniformHandle() : info(nullptr), manager(nullptr) {}
    UniformHandle(void* info, void* manager)
        : info(info), manager(manager) {}

    // Direct field access: uniform["fieldName"] = value
    FieldProxy operator[](const eastl::string& fieldName);
    FieldProxy operator[](const char* fieldName) {
        return operator[](eastl::string(fieldName));
    }

    vk::DescriptorSet getSet() const;
    uint32_t getDynamicOffset() const;
    bool isValid() const { return info != nullptr; }

private:
    void* info;    // Points to DescriptorManager::UniformSetInfo
    void* manager; // Points to DescriptorManager
};

// Central descriptor management system with declarative API
class DescriptorManager {
    friend class UniformHandle;  // Allow UniformHandle to access private members

public:
    void init(VulkanContext* context, uint32_t maxFramesInFlight);
    void cleanup();

    // Register layout, returns handle (automatically deduplicates based on hash)
    LayoutHandle registerLayout(const DescriptorLayoutDesc& desc);

    // Allocate descriptor set from registered layout
    vk::DescriptorSet allocateSet(LayoutHandle handle, uint32_t frameIndex);

    // Get layout for pipeline creation
    vk::DescriptorSetLayout getLayout(LayoutHandle handle) const;

    bool hasLayout(LayoutHandle handle) const;

    // @deprecated Legacy string-based API - use LayoutHandle-based API instead
    // TODO: Remove once all code migrates to hash-based LayoutHandle
    eastl::vector<vk::DescriptorSet> allocateSets(const eastl::string& layoutName, uint32_t count);
    vk::DescriptorSetLayout getLayout(const eastl::string& layoutName) const;
    bool hasLayout(const eastl::string& layoutName) const;
    void updateSet(vk::DescriptorSet set, const eastl::vector<ResourceBindingDesc>& bindings);

    // Sampler management - centralized sampler creation and caching
    vk::Sampler getSampler(SamplerType type);
    vk::Sampler getOrCreateSampler(const SamplerConfig& config);

    // @deprecated Bindless texture management - will be replaced by reflection-based descriptors
    // TODO: Migrate to descriptor set per shader with automatic binding from reflection
    void initBindless(uint32_t maxTextures);
    uint32_t allocateBindlessTexture(Texture* texture);
    uint32_t allocateBindlessTextureAt(Texture* texture, uint32_t index);
    uint32_t allocateBindlessCubemap(Texture* cubemapTexture);
    void freeBindlessTexture(uint32_t index);
    void freeBindlessCubemap(uint32_t index);
    vk::DescriptorSet getBindlessSet() const;
    bool isBindlessEnabled() const { return bindlessEnabled; }

    // @deprecated Material data SSBO management - will be replaced by per-material descriptors
    // TODO: Remove once MaterialManager uses reflection-based descriptor updates
    struct MaterialData {
        // Material parameters
        alignas(16) glm::vec4 baseColorFactor{1.0f};
        alignas(4) float metallicFactor{1.0f};  // glTF 2.0 default
        alignas(4) float roughnessFactor{1.0f};  // glTF 2.0 default
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

    // @deprecated Material data buffer API - will be replaced by reflection-based updates
    bool initMaterialDataBuffer(uint32_t maxMaterials = 1024);
    uint32_t allocateMaterialData(const MaterialData& data);
    bool updateMaterialData(uint32_t index, const MaterialData& data);
    bool freeMaterialData(uint32_t index);
    vk::DescriptorSet getMaterialDataSet() const { return materialDataSet; }
    bool isMaterialDataEnabled() const { return materialDataEnabled; }
    const MaterialData* getMaterialData(uint32_t index) const;
    uint32_t getMaxMaterialData() const { return maxMaterialData; }

    // Reflection-based descriptor API (modern, preferred)
    void setReflection(LayoutHandle handle, const ShaderReflection& reflection);
    const ShaderReflection* getReflection(LayoutHandle handle) const;
    bool hasReflection(LayoutHandle handle) const;

    // ===== Reflection-Based Uniform Management API =====
    // Set current frame for internal tracking (eliminates need to pass frameIndex everywhere)
    void setCurrentFrame(uint32_t frameIndex);

    // Create a managed uniform buffer with reflection support
    // Returns UniformHandle for field-based updates
    UniformHandle createUniform(const eastl::string& name, LayoutHandle layout, UpdateFrequency frequency);

    // Get uniform by name (uses currentFrame for PerFrame uniforms)
    UniformHandle getUniform(const eastl::string& name);

    // Get uniform by name with explicit frame override
    UniformHandle getUniform(const eastl::string& name, uint32_t frameIndex);

    // ===== Push Constant Management API =====
    // Register push constants, returns handle (automatically deduplicates based on hash)
    PushConstantHandle registerPushConstants(const PushConstantDesc& desc);

    // Get push constants for pipeline creation
    const eastl::vector<vk::PushConstantRange>& getPushConstants(PushConstantHandle handle) const;

    bool hasPushConstants(PushConstantHandle handle) const;

    // ===== PipelineLayout Cache & Named Binding API =====

    // 命名descriptor（统一接口）
    struct NamedDescriptor {
        const char* name;                 // Reflection中的名称
        vk::DescriptorSet descriptorSet;  // 实际的descriptor set
        uint32_t dynamicOffset;           // Dynamic offset（bindless为0）

        // 便利构造函数
        static NamedDescriptor fromUniform(const char* name, const UniformHandle& uniform) {
            return {name, uniform.getSet(), uniform.getDynamicOffset()};
        }

        static NamedDescriptor fromSet(const char* name, vk::DescriptorSet set) {
            return {name, set, 0};
        }
    };

    // 获取或创建PipelineLayoutCache（自动检测layout组合并复用）
    PipelineLayoutCacheHandle getOrCreatePipelineLayoutCache(
        eastl::shared_ptr<class Shader> vertShader,
        eastl::shared_ptr<class Shader> fragShader = nullptr
    );

    // 统一绑定API（使用命名descriptor）
    void bindDescriptors(
        vk::CommandBuffer cmd,
        PipelineLayoutCacheHandle cacheHandle,  // 从getOrCreatePipelineLayoutCache获取
        vk::PipelineLayout pipelineLayout,
        vk::PipelineBindPoint bindPoint,
        const eastl::vector<NamedDescriptor>& descriptors
    );

private:
    struct LayoutInfo {
        vk::DescriptorSetLayout layout;
        UpdateFrequency frequency;
        eastl::vector<vk::DescriptorPoolSize> poolSizes;
        vk::DescriptorSetLayoutCreateFlags createFlags;
        ShaderReflection reflection;  // Reflection data for UBO/SSBO fields
    };

    struct PoolInfo {
        vk::DescriptorPool pool;
        uint32_t remainingSets = 0;
        uint32_t maxSets = 0;
    };

    // Internal structure for managed uniforms
    struct UniformSetInfo {
        BufferResource buffer;           // VMA-managed buffer with auto-mapping
        vk::DescriptorSet descriptorSet; // Descriptor set bound to buffer
        LayoutHandle layoutHandle;       // For reflection lookup
        UpdateFrequency frequency;       // Update frequency
        uint32_t bufferSize;            // Total buffer size
        uint32_t alignedSize;           // Size aligned to minUniformBufferOffsetAlignment (for PerFrame)
        eastl::string name;             // For debugging and lookup
    };

    void createPool(UpdateFrequency frequency);
    void growPool(UpdateFrequency frequency);
    vk::DescriptorPool getOrCreatePool(UpdateFrequency frequency);

    VulkanContext* context = nullptr;
    uint32_t maxFrames = 0;
    uint32_t currentFrame = 0;  // Track current frame for automatic offset calculation

    eastl::unordered_map<LayoutHandle, LayoutInfo> layouts;
    eastl::unordered_map<eastl::string, LayoutHandle> nameToHandle;  // For legacy string-based API
    eastl::unordered_map<UpdateFrequency, eastl::vector<PoolInfo>> poolsByFrequency;

    // Bindless texture management
    bool bindlessEnabled = false;
    vk::DescriptorSet bindlessSet;
    eastl::vector<Texture*> bindlessTextureSlots;   // 2D textures (binding 0)
    eastl::vector<uint32_t> bindlessFreeIndices;
    uint32_t bindlessMaxTextures = 0;

    // Cubemap bindless (binding 1)
    eastl::vector<Texture*> bindlessCubemapSlots;
    eastl::vector<uint32_t> bindlessCubemapFreeIndices;
    uint32_t bindlessMaxCubemaps = 64;  // Fewer cubemaps needed than 2D textures

    // Material data SSBO management
    bool materialDataEnabled = false;
    vk::DescriptorSet materialDataSet;

    // Sampler cache - avoid creating duplicate samplers
    eastl::unordered_map<size_t, vk::Sampler> samplerCache;  // Hash -> Sampler
    eastl::unordered_map<SamplerType, vk::Sampler> predefinedSamplers;  // Type -> Sampler

    // Internal sampler creation
    vk::Sampler createSampler(const SamplerConfig& config);
    BufferResource materialDataBuffer;  // Managed by ResourceFactory
    void* materialDataMapped = nullptr;  // Persistent mapping from VMA
    eastl::vector<MaterialData> materialDataSlots;
    eastl::vector<uint32_t> materialDataFreeIndices;
    uint32_t maxMaterialData = 0;

    // Reflection-based uniform management (LayoutHandle -> single UniformSetInfo)
    // PerFrame uses dynamic offset, so only one descriptor set per layout
    eastl::unordered_map<LayoutHandle, UniformSetInfo> uniforms;

    // Push constant management (handle -> ranges)
    eastl::unordered_map<PushConstantHandle, eastl::vector<vk::PushConstantRange>> pushConstants;

    // PipelineLayout cache for descriptor binding (handle -> cache)
    struct PipelineLayoutCache {
        eastl::unordered_map<eastl::string, uint32_t> resourceNameToSet;
        eastl::unordered_set<uint32_t> bindlessSets;
        eastl::vector<LayoutHandle> layoutHandles;  // 稀疏，按set index
        PushConstantHandle pushConstantHandle;
    };
    eastl::unordered_map<PipelineLayoutCacheHandle, PipelineLayoutCache> pipelineLayoutCache;  // Layout组合hash -> cache

    //todo remove
    // Pool size configuration per frequency
    static constexpr uint32_t POOL_SIZE_PER_FRAME = 10;
    static constexpr uint32_t POOL_SIZE_PER_PASS = 20;
    static constexpr uint32_t POOL_SIZE_PER_MATERIAL = 100;
    static constexpr uint32_t POOL_SIZE_STATIC = 50;
};

} // namespace violet
