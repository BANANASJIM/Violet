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
using SetGroupHandle = uint64_t;  // Handle to a group of descriptor sets (auto-managed by frequency)

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
        Buffer,                // UniformBuffer or StorageBuffer
        Texture,               // CombinedImageSampler (Texture* contains imageView+sampler)
        SampledImage,          // Sampled image (read-only, no sampler)
        ImageView,             // StorageImage (RW)
        Sampler,               // Separate sampler
        CombinedImageSampler   // ImageView + Sampler specified separately
    };

    Type type;

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

    // Factory methods
    static DescriptorResourceHandle fromBuffer(vk::Buffer buf, vk::DeviceSize offset, vk::DeviceSize range);
    static DescriptorResourceHandle fromBuffer(const BufferResource& buf);
    static DescriptorResourceHandle fromTexture(Texture* tex);
    static DescriptorResourceHandle fromSampledImage(vk::ImageView view, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
    static DescriptorResourceHandle fromStorageImage(vk::ImageView view);
    static DescriptorResourceHandle fromSampler(vk::Sampler samp);
    static DescriptorResourceHandle fromCombinedImageSampler(vk::ImageView view, vk::Sampler samp,
                                                   vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
};

// ===== Push Constants =====

struct PushConstantDesc {
    eastl::vector<vk::PushConstantRange> ranges;
    PushConstantHandle hash() const;
};

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

    // New API: Allocate descriptor set group (auto-manages frame count based on frequency)
    // - PerFrame → allocates maxFrames sets
    // - Other frequencies → allocates 1 set
    // Returns handle to the set group for later access
    SetGroupHandle allocateSetGroup(LayoutHandle layoutHandle);

    // Get descriptor set for specific frame from set group
    // - PerFrame: returns sets[frameIndex]
    // - Other: returns sets[0]
    vk::DescriptorSet getSet(SetGroupHandle handle, uint32_t frameIndex = 0) const;

    // Free set group and all its descriptor sets
    void freeSetGroup(SetGroupHandle handle);

    // Legacy API: Allocate single descriptor set for the given layout
    // Note: For PerFrame resources, only 1 set is allocated. Use dynamic offsets at bind time.
    // @deprecated Use allocateSetGroup() instead
    vk::DescriptorSet allocateSet(LayoutHandle handle);

    // ===== High-Level Automatic Binding Interface =====

    // **RECOMMENDED API**: Fully automatic descriptor set management
    // Allocates (if needed) → Updates (if dirty) → Binds to command buffer
    // All information is derived from the ShaderResourceBinding's associated pipeline
    void bindResources(vk::CommandBuffer cmd, class ShaderResourceBinding& binding, uint32_t frameIndex);

    // ===== Unified Reflection-Driven Binding Interface =====

    // New API: Update descriptor set using ShaderResourceBinding
    // Automatically selects the correct set from the group based on frameIndex
    void updateSetFromBinding(SetGroupHandle handle, const class ShaderResourceBinding& binding,
                             const ShaderReflection& reflection, uint32_t frameIndex = 0);

    // Legacy API: bind resources using reflection-driven map
    // All binding numbers and types are queried from reflection
    void updateSet(vk::DescriptorSet set,
                   const ShaderReflection& reflection,
                   const eastl::unordered_map<eastl::string, DescriptorResourceHandle>& resources);

    // Helper methods: bind individual resources by name (uses reflection)
    void bindBuffer(vk::DescriptorSet set, const eastl::string& resourceName,
                   const BufferResource& buffer, const ShaderReflection& reflection);
    void bindTexture(vk::DescriptorSet set, const eastl::string& resourceName,
                    Texture* texture, const ShaderReflection& reflection);
    void bindStorageImage(vk::DescriptorSet set, const eastl::string& resourceName,
                         vk::ImageView imageView, const ShaderReflection& reflection);

    // ===== Direct Resource Binding (bypassesreflection) =====

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
        eastl::vector<BindingDesc> bindings;  // Store original bindings for stage merging
    };

    struct PoolInfo {
        vk::DescriptorPool pool;
        uint32_t remainingSets = 0;
        uint32_t maxSets = 0;
    };

    struct DescriptorSetGroup {
        eastl::vector<vk::DescriptorSet> sets;  // PerFrame: maxFrames sets, other: 1 set
        LayoutHandle layoutHandle;
        UpdateFrequency frequency;
    };

    // ===== Internal Methods =====

    void createPool(UpdateFrequency frequency);
    void growPool(UpdateFrequency frequency);
    vk::DescriptorPool getOrCreatePool(UpdateFrequency frequency);
    void updateBindlessDescriptor(Texture* texture, uint32_t binding, uint32_t arrayIndex);

    // ===== Member Variables =====

    VulkanContext* context = nullptr;
    uint32_t maxFrames = 0;
    uint32_t currentFrame = 0;

    // Layout management
    eastl::unordered_map<LayoutHandle, LayoutInfo> layouts;
    eastl::unordered_map<eastl::string, LayoutHandle> nameToHandle;

    // Descriptor pool management
    eastl::unordered_map<UpdateFrequency, eastl::vector<PoolInfo>> poolsByFrequency;

    // Set group management
    eastl::unordered_map<SetGroupHandle, DescriptorSetGroup> setGroups;
    SetGroupHandle nextSetGroupHandle = 1;

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