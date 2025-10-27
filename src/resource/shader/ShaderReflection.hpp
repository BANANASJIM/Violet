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
struct ReflectedBuffer {
    eastl::string name;
    uint32_t binding;
    uint32_t set;
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

    // Buffer layout (only valid for UBO/SSBO, nullptr for textures/images/samplers)
    const ReflectedBuffer* bufferLayout;

    ReflectedResource()
        : type(vk::DescriptorType::eUniformBuffer), set(0), binding(0),
          arraySize(1), stages(), isBindless(false), bufferLayout(nullptr) {}
};

// Shader reflection container
class ShaderReflection {
public:
    // === Legacy Buffer API (UBO/SSBO field access) ===
    void addBuffer(const ReflectedBuffer& buffer) {
        buffers.push_back(buffer);
    }

    const ReflectedBuffer* findBuffer(const eastl::string& name) const {
        for (const auto& buf : buffers) {
            if (buf.name == name) return &buf;
        }
        return nullptr;
    }

    const ReflectedField* findField(const eastl::string& bufferName, const eastl::string& fieldName) const {
        const ReflectedBuffer* buf = findBuffer(bufferName);
        if (!buf) return nullptr;

        for (const auto& field : buf->fields) {
            if (field.name == fieldName) return &field;
        }
        return nullptr;
    }

    const eastl::vector<ReflectedBuffer>& getBuffers() const { return buffers; }

    // === Unified Resource API (all descriptor types) ===

    void addResource(const ReflectedResource& resource) {
        resources.push_back(resource);

        // Also add to set-grouped map
        resourcesBySet[resource.set].push_back(resource);

        // Build name index
        resourceByName[resource.name] = &resources.back();
    }

    const ReflectedResource* findResource(const eastl::string& name) const {
        auto it = resourceByName.find(name);
        return (it != resourceByName.end()) ? it->second : nullptr;
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

    void clear() {
        buffers.clear();
        resources.clear();
        resourcesBySet.clear();
        resourceByName.clear();
    }

private:
    // Legacy: Detailed buffer layouts (UBO/SSBO)
    eastl::vector<ReflectedBuffer> buffers;

    // Unified: All resources (including buffers, textures, images, samplers)
    eastl::vector<ReflectedResource> resources;
    eastl::unordered_map<uint32_t, eastl::vector<ReflectedResource>> resourcesBySet;
    eastl::unordered_map<eastl::string, const ReflectedResource*> resourceByName;
};

// Extract reflection from Slang (opaque pointer to avoid slang.h dependency)
bool extractReflection(void* slangProgramLayout, ShaderReflection& reflection);

} // namespace violet