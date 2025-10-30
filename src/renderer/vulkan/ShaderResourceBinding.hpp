#pragma once

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include "renderer/vulkan/DescriptorManager.hpp"

namespace violet {

class BufferResource;
class Texture;
class ShaderReflection;
class GraphicsPipeline;
class ComputePipeline;

/**
 * @brief Shader Resource Binding - Automatic descriptor set management
 *
 * This class provides a high-level interface for binding resources to shaders.
 * It automatically manages descriptor set allocation, updates, and binding.
 *
 * Lifecycle: Per-object (per-material, per-pass, per-frame)
 * - Per-frame bindings: Global UBO, lights, shadows
 * - Per-pass bindings: Render targets, compute outputs
 * - Per-draw bindings: Material data, mesh-specific resources
 *
 * Usage:
 * 1. Initialize with pipeline and set index:
 *    ShaderResourceBinding binding;
 *    binding.init(pipeline, 0);  // Use set 0 of this pipeline
 *
 * 2. Bind resources by name:
 *    binding.bindBuffer("global", globalUBO);
 *    binding.bindTexture("albedoMap", albedoTexture);
 *
 * 3. Automatic descriptor management (one call does everything):
 *    descriptorManager->bindResources(cmd, binding, frameIndex);
 *    // This automatically: allocates sets (if needed) → updates (if dirty) → binds to cmd
 *
 * 4. No manual descriptor set management needed!
 */
class ShaderResourceBinding {
public:
    ShaderResourceBinding() = default;
    ~ShaderResourceBinding() = default;

    // ===== Initialization =====

    // Initialize with graphics pipeline and set index
    void init(GraphicsPipeline* pipeline, uint32_t setIndex);

    // Initialize with compute pipeline and set index
    void init(ComputePipeline* pipeline, uint32_t setIndex);

    // Check if initialized
    bool isInitialized() const { return graphicsPipeline != nullptr || computePipeline != nullptr; }

    // ===== Resource Binding API =====

    // Bind buffer resource by name (reflection-based)
    void bindBuffer(const eastl::string& name, const BufferResource* buffer,
                   vk::DeviceSize offset = 0, vk::DeviceSize range = VK_WHOLE_SIZE);

    // Bind texture resource by name
    void bindTexture(const eastl::string& name, Texture* texture);

    // Bind sampled image by name (for separate sampler/texture bindings)
    void bindSampledImage(const eastl::string& name, vk::ImageView imageView,
                          vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);

    // Bind storage image by name
    void bindStorageImage(const eastl::string& name, vk::ImageView imageView);

    // Bind sampler by name
    void bindSampler(const eastl::string& name, vk::Sampler sampler);

    // Bind combined image sampler by name
    void bindCombinedImageSampler(const eastl::string& name, vk::ImageView imageView,
                                  vk::Sampler sampler, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);

    // ===== Resource Query =====

    // Get all bound resources (for descriptor updates)
    const eastl::unordered_map<eastl::string, DescriptorResourceHandle>& getResources() const {
        return resources;
    }

    // Check if a resource is bound
    bool hasResource(const eastl::string& name) const {
        return resources.find(name) != resources.end();
    }

    // Get specific resource handle (returns nullptr if not found)
    const DescriptorResourceHandle* getResource(const eastl::string& name) const {
        auto it = resources.find(name);
        return it != resources.end() ? &it->second : nullptr;
    }

    // ===== Dirty Tracking (manual) =====

    void markDirty() { dirty = true; }
    void clearDirty() { dirty = false; }
    bool isDirty() const { return dirty; }

    // ===== Cache Key Generation =====

    // Generate hash key for this binding configuration (for caching/reuse)
    // Hash is based on resource names + types, not actual resource pointers
    uint64_t generateCacheKey() const;

    // Clear all bindings
    void clear() {
        resources.clear();
        dirty = true;
    }

    // Get number of bound resources
    size_t getResourceCount() const { return resources.size(); }

    // ===== Internal Access (for DescriptorManager) =====

    vk::PipelineLayout getPipelineLayout() const;
    const ShaderReflection* getShaderReflection() const;
    GraphicsPipeline* getGraphicsPipeline() const { return graphicsPipeline; }
    ComputePipeline* getComputePipeline() const { return computePipeline; }
    LayoutHandle getLayoutHandle() const { return layoutHandle; }
    uint32_t getSetIndex() const { return setIndex; }
    SetGroupHandle getSetGroupHandle() const { return setGroupHandle; }
    void setSetGroupHandle(SetGroupHandle handle) { setGroupHandle = handle; }

private:
    // Pipeline association (one or the other, not both)
    GraphicsPipeline* graphicsPipeline = nullptr;
    ComputePipeline* computePipeline = nullptr;

    // Cached shader info (for fast access)
    eastl::weak_ptr<class Shader> shader;
    LayoutHandle layoutHandle = 0;

    // Which descriptor set in the pipeline's layout
    uint32_t setIndex = 0;

    // Internal descriptor set group handle (managed by DescriptorManager)
    SetGroupHandle setGroupHandle = 0;

    // Resource name → GPU resource handle mapping
    eastl::unordered_map<eastl::string, DescriptorResourceHandle> resources;

    // Dirty flag (automatic tracking)
    bool dirty = true;
};

} // namespace violet