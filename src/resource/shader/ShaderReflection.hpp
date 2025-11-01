#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <vulkan/vulkan.hpp>
#include <cstdint>

namespace violet {

enum class FieldType : uint8_t {
    Float, Vec2, Vec3, Vec4,
    Int, IVec2, IVec3, IVec4,
    UInt, UVec2, UVec3, UVec4,
    Mat4,
    Unknown
};

// Field in UBO/SSBO
struct ReflectedField {
    eastl::string name;
    uint32_t offset;
    uint32_t size;
    FieldType type;
};

// UBO/SSBO layout (detailed field information)
// Note: name/binding/set are stored in ReflectedResource, accessed via bufferLayoutIndex
struct ReflectedBuffer {
    uint32_t totalSize;
    eastl::vector<ReflectedField> fields;
};

// Unified resource descriptor (all descriptor types)
struct ReflectedResource {
    eastl::string name;              // Resource name (variable name in shader)
    vk::DescriptorType type;         // Descriptor type
    uint32_t set;                    // Descriptor set index
    uint32_t binding;                // Binding index
    uint32_t arraySize;              // Array size (1 = not array, 0 = unbounded array)
    vk::ShaderStageFlags stages;     // Shader stage flags
    bool isBindless;                 // Is bindless resource (large/unbounded array)

    // Buffer layout index (only valid for UBO/SSBO, ~0u for textures/images/samplers)
    size_t bufferLayoutIndex;

    ReflectedResource()
        : type(vk::DescriptorType::eUniformBuffer), set(0), binding(0),
          arraySize(1), stages(), isBindless(false), bufferLayoutIndex(~0u) {}
};

// Shader reflection container
class ShaderReflection {
public:
    // === Unified Resource API (all descriptor types) ===

    void addResource(const ReflectedResource& resource) {
        // Check for duplicates by (set, binding) pair
        auto& setResources = resourcesBySet[resource.set];
        for (size_t i = 0; i < setResources.size(); ++i) {
            if (setResources[i].binding == resource.binding) {
                // Duplicate found - update stages in both vectors
                // This happens when Slang exposes same resource from multiple scopes

                // Find the index in main resources vector
                for (size_t j = 0; j < resources.size(); ++j) {
                    if (resources[j].set == resource.set && resources[j].binding == resource.binding) {
                        resources[j].stages |= resource.stages;
                        setResources[i].stages |= resource.stages;
                        return;
                    }
                }
                return;
            }
        }

        // No duplicate - add new resource and store index
        size_t newIndex = resources.size();
        resources.push_back(resource);
        resourcesBySet[resource.set].push_back(resource);
        resourceByName[resource.name] = newIndex;
    }

    const ReflectedResource* findResource(const eastl::string& name) const {
        auto it = resourceByName.find(name);
        if (it != resourceByName.end()) {
            size_t index = it->second;
            if (index < resources.size()) {
                return &resources[index];
            }
        }
        return nullptr;
    }

    // Find resource by (set, binding) tuple
    const ReflectedResource* findResourceByBinding(uint32_t set, uint32_t binding) const {
        auto setIt = resourcesBySet.find(set);
        if (setIt != resourcesBySet.end()) {
            for (const auto& resource : setIt->second) {
                if (resource.binding == binding) {
                    return &resource;
                }
            }
        }
        return nullptr;
    }

    const eastl::vector<ReflectedResource>& getResourcesBySet(uint32_t set) const {
        static eastl::vector<ReflectedResource> empty;
        auto it = resourcesBySet.find(set);
        return (it != resourcesBySet.end()) ? it->second : empty;
    }

    const eastl::vector<ReflectedResource>& getAllResources() const {
        return resources;
    }

    const eastl::unordered_map<uint32_t, eastl::vector<ReflectedResource>>& getResourcesBySetMap() const {
        return resourcesBySet;
    }

    const eastl::vector<ReflectedBuffer>& getAllBuffers() const {
        return bufferStorage;
    }

    // Get buffer layout by index (nullptr if invalid)
    const ReflectedBuffer* getBufferLayout(size_t index) const {
        if (index < bufferStorage.size()) {
            return &bufferStorage[index];
        }
        return nullptr;
    }

    // Get buffer layout by name (nullptr if not found)
    // Searches for resource with given name, then returns its buffer layout
    const ReflectedBuffer* getBufferLayout(const eastl::string& name) const {
        const ReflectedResource* res = findResource(name);
        if (res && res->bufferLayoutIndex != ~0u) {
            return getBufferLayout(res->bufferLayoutIndex);
        }
        return nullptr;
    }

    // === Push Constants API ===

    void addPushConstantRange(const vk::PushConstantRange& range) {
        pushConstantRanges.push_back(range);
    }

    const eastl::vector<vk::PushConstantRange>& getPushConstantRanges() const {
        return pushConstantRanges;
    }

    bool hasPushConstants() const {
        return !pushConstantRanges.empty();
    }

    void clear() {
        resources.clear();
        resourcesBySet.clear();
        resourceByName.clear();
        bufferStorage.clear();
        pushConstantRanges.clear();
    }

    // Internal: Store buffer and return index (for extractReflection)
    size_t storeBuffer(const ReflectedBuffer& buffer) {
        size_t index = bufferStorage.size();
        bufferStorage.push_back(buffer);
        return index;
    }

private:
    // All resources (UBO, SSBO, textures, images, samplers)
    eastl::vector<ReflectedResource> resources;
    eastl::unordered_map<uint32_t, eastl::vector<ReflectedResource>> resourcesBySet;
    eastl::unordered_map<eastl::string, size_t> resourceByName;  // Maps name -> index in resources vector

    // Buffer storage (owned by ShaderReflection, pointed to by ReflectedResource::bufferLayout)
    eastl::vector<ReflectedBuffer> bufferStorage;

    // Push constant ranges
    eastl::vector<vk::PushConstantRange> pushConstantRanges;
};

// Extract reflection from Slang (opaque pointer to avoid slang.h dependency)
bool extractReflection(void* slangProgramLayout, ShaderReflection& reflection);

} // namespace violet