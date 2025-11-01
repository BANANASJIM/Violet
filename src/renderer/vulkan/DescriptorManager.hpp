#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/shared_ptr.h>
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/shader/ShaderReflection.hpp"
#include "renderer/vulkan/SamplerManager.hpp"
#include "core/Log.hpp"

namespace violet {

class VulkanContext;
class Texture;
class Shader;

using LayoutHandle = uint32_t;
using PushConstantHandle = uint32_t;

struct BindingKey {
    uint32_t set;
    uint32_t binding;
    uint32_t arrayIndex = 0;

    bool operator==(const BindingKey& other) const {
        return set == other.set && binding == other.binding && arrayIndex == other.arrayIndex;
    }
};

struct SetCacheKey {
    LayoutHandle layout;
    size_t ownerID;

    bool operator==(const SetCacheKey& other) const {
        return layout == other.layout && ownerID == other.ownerID;
    }
};

// ===== Descriptor Layout Description =====

enum class UpdateFrequency {
    PerFrame,      // Updates every frame (camera, time)
    PerPass,       // Updates per render pass (shadow maps, render targets)
    PerMaterial,   // Updates when material changes (material properties, textures)
    Static         // Rarely updates (bindless arrays, global resources)
};

struct BindingDesc {
    uint32_t binding;
    vk::DescriptorType type;
    vk::ShaderStageFlags stages;
    uint32_t count = 1;  // Array size (1 for single descriptor)
    vk::DescriptorBindingFlags flags = {};
};

struct DescriptorLayoutDesc {
    eastl::string name;  // For debugging only, not used in hash
    eastl::vector<BindingDesc> bindings;
    UpdateFrequency frequency = UpdateFrequency::PerMaterial;
    vk::DescriptorSetLayoutCreateFlags flags = {};
    bool isBindless = false;

    LayoutHandle hash() const;
};

// ===== Resource Handle (unified resource abstraction) =====

struct DescriptorResourceHandle {
    enum class Type {
        Buffer,
        Texture,
        SampledImage,
        ImageView,
        Sampler,
        CombinedImageSampler
    };

    Type type;
    UpdateFrequency frequency = UpdateFrequency::Static;

    union {
        struct {
            vk::Buffer buffer;
            vk::DeviceSize offset;
            vk::DeviceSize range;
        } bufferData;

        Texture* texture;

        struct {
            vk::ImageView imageView;
            vk::Sampler sampler;
        } combinedData;

        vk::ImageView imageView;
        vk::Sampler sampler;
    };

    vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    DescriptorResourceHandle() : type(Type::Buffer), bufferData{nullptr, 0, 0} {}

    static DescriptorResourceHandle fromBuffer(vk::Buffer buf, vk::DeviceSize offset, vk::DeviceSize range,
                                                UpdateFrequency freq = UpdateFrequency::Static);
    static DescriptorResourceHandle fromBuffer(const BufferResource& buf,
                                                UpdateFrequency freq = UpdateFrequency::Static);
    static DescriptorResourceHandle fromTexture(Texture* tex,
                                                 UpdateFrequency freq = UpdateFrequency::Static);
    static DescriptorResourceHandle fromSampledImage(vk::ImageView view,
                                                      vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                                      UpdateFrequency freq = UpdateFrequency::Static);
    static DescriptorResourceHandle fromStorageImage(vk::ImageView view,
                                                      UpdateFrequency freq = UpdateFrequency::Static);
    static DescriptorResourceHandle fromSampler(vk::Sampler samp,
                                                 UpdateFrequency freq = UpdateFrequency::Static);
    static DescriptorResourceHandle fromCombinedImageSampler(vk::ImageView view, vk::Sampler samp,
                                                              vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                                              UpdateFrequency freq = UpdateFrequency::Static);
};

// ===== Push Constants =====

struct PushConstantDesc {
    eastl::vector<vk::PushConstantRange> ranges;
    PushConstantHandle hash() const;
};

} // namespace violet

// Hash functions (must be before DescriptorManager uses them)
template<>
struct eastl::hash<violet::BindingKey> {
    size_t operator()(const violet::BindingKey& key) const {
        size_t h = 0;
        h ^= eastl::hash<uint32_t>{}(key.set) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= eastl::hash<uint32_t>{}(key.binding) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= eastl::hash<uint32_t>{}(key.arrayIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

template<>
struct eastl::hash<violet::SetCacheKey> {
    size_t operator()(const violet::SetCacheKey& key) const {
        size_t h = 0;
        h ^= eastl::hash<uint32_t>{}(key.layout) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= eastl::hash<size_t>{}(key.ownerID) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

namespace violet {

// ===== Central Descriptor Management System =====

class DescriptorManager {
public:
    void init(VulkanContext* context, uint32_t maxFramesInFlight);
    void cleanup();

    VulkanContext* getContext() const { return context; }

    // ===== Descriptor Layout Management =====

    // Register layout, returns handle (auto-deduplicates via hash)
    LayoutHandle registerLayout(const DescriptorLayoutDesc& desc);

    // Query layout information
    vk::DescriptorSetLayout getLayout(LayoutHandle handle) const;
    bool hasLayout(LayoutHandle handle) const;
    LayoutHandle getLayoutHandle(const eastl::string& name) const;

    // Legacy string-based API (deprecated)
    eastl::vector<vk::DescriptorSet> allocateSets(const eastl::string& layoutName, uint32_t count);
    vk::DescriptorSetLayout getLayout(const eastl::string& layoutName) const;
    bool hasLayout(const eastl::string& layoutName) const;

    // ===== Descriptor Set Management =====

    vk::DescriptorSet allocateSet(LayoutHandle handle);

    // ===== New Unified API =====

    void update(const class ShaderResourceBinding& srb, const class PipelineBase* pipeline);
    void bind(vk::CommandBuffer cmd, const class ShaderResourceBinding& srb, const class PipelineBase* pipeline);

    // ===== Internal/Low-Level APIs =====

    void updateSet(vk::DescriptorSet set,
                   const ShaderReflection& reflection,
                   const eastl::unordered_map<struct BindingKey, DescriptorResourceHandle>& resources);

    void updateSet(vk::DescriptorSet set,
                   const ShaderReflection& reflection,
                   const eastl::unordered_map<eastl::string, DescriptorResourceHandle>& resources);

    void bindBuffer(vk::DescriptorSet set, uint32_t binding,
                   const BufferResource& buffer, vk::DescriptorType type,
                   vk::DeviceSize offset = 0, vk::DeviceSize range = VK_WHOLE_SIZE);
    void bindTexture(vk::DescriptorSet set, uint32_t binding, Texture* texture);
    void bindStorageImage(vk::DescriptorSet set, uint32_t binding, vk::ImageView imageView);
    void bindSampler(vk::DescriptorSet set, uint32_t binding, vk::Sampler sampler);

    // ===== Bindless Resource Management =====

    void initBindless(uint32_t maxTextures, LayoutHandle bindlessLayoutHandle);
    void initBindlessSamplers();  // Initialize global samplers in bindless set (binding 2, 3, 4)
    uint32_t allocateBindlessTexture(Texture* texture);
    uint32_t allocateBindlessTextureAt(Texture* texture, uint32_t index);
    uint32_t allocateBindlessCubemap(Texture* cubemap);
    void freeBindlessTexture(uint32_t index);
    void freeBindlessCubemap(uint32_t index);
    vk::DescriptorSet getBindlessSet() const;
    bool isBindlessEnabled() const { return bindlessEnabled; }

    // ===== Frame Management =====

    void setCurrentFrame(uint32_t frameIndex);
    uint32_t getCurrentFrame() const { return currentFrame; }

    // ===== Sampler Management =====

    SamplerManager& getSamplerManager() { return samplerManager; }

    // ===== Push Constant Management =====

    PushConstantHandle registerPushConstants(const PushConstantDesc& desc);
    const eastl::vector<vk::PushConstantRange>& getPushConstants(PushConstantHandle handle) const;
    bool hasPushConstants(PushConstantHandle handle) const;

private:
    // ===== Internal Data Structures =====

    struct LayoutInfo {
        vk::DescriptorSetLayout layout;
        UpdateFrequency frequency;
        eastl::vector<vk::DescriptorPoolSize> poolSizes;
        vk::DescriptorSetLayoutCreateFlags createFlags;
        eastl::vector<BindingDesc> bindings;
    };

    struct PoolInfo {
        vk::DescriptorPool pool;
        uint32_t remainingSets = 0;
        uint32_t maxSets = 0;
    };

    // ===== Internal Methods =====

    void createPool(UpdateFrequency frequency);
    void growPool(UpdateFrequency frequency);
    vk::DescriptorPool getOrCreatePool(UpdateFrequency frequency);
    void updateBindlessDescriptor(Texture* texture, uint32_t binding, uint32_t arrayIndex);

    UpdateFrequency getMaxFrequencyForSet(const eastl::unordered_map<BindingKey, DescriptorResourceHandle>& resources, uint32_t setIndex);
    size_t computeOwnerID(const class ShaderResourceBinding& srb, uint32_t setIndex, UpdateFrequency maxFreq);
    vk::DescriptorSet getOrCreateCachedSet(LayoutHandle handle, size_t ownerID);
    void updateDescriptorInternal(vk::DescriptorSet set, const BindingKey& key, const DescriptorResourceHandle& resource);

    // ===== Member Variables =====

    VulkanContext* context = nullptr;
    uint32_t maxFrames = 0;
    uint32_t currentFrame = 0;

    // Layout management
    eastl::unordered_map<LayoutHandle, LayoutInfo> layouts;
    eastl::unordered_map<eastl::string, LayoutHandle> nameToHandle;

    // Descriptor pool management
    eastl::unordered_map<UpdateFrequency, eastl::vector<PoolInfo>> poolsByFrequency;

    // Descriptor set cache
    eastl::unordered_map<SetCacheKey, vk::DescriptorSet> setCache;

    // Bindless texture management
    bool bindlessEnabled = false;
    vk::DescriptorSet bindlessSet;
    eastl::vector<Texture*> bindlessTextureSlots;
    eastl::vector<uint32_t> bindlessFreeIndices;
    uint32_t bindlessMaxTextures = 0;
    eastl::vector<Texture*> bindlessCubemapSlots;
    eastl::vector<uint32_t> bindlessCubemapFreeIndices;
    uint32_t bindlessMaxCubemaps = 64;

    SamplerManager samplerManager;

    // Push constants
    eastl::unordered_map<PushConstantHandle, eastl::vector<vk::PushConstantRange>> pushConstants;
};

} // namespace violet