#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

namespace violet {

class VulkanContext;
class Mesh;

// Mesh handle for resource management
struct MeshHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    bool isValid() const { return index > 0; }
    bool operator==(const MeshHandle& other) const {
        return index == other.index && generation == other.generation;
    }
};

class MeshManager {
public:
    MeshManager() = default;
    ~MeshManager();

    MeshManager(const MeshManager&) = delete;
    MeshManager& operator=(const MeshManager&) = delete;

    void init(VulkanContext* ctx);
    void cleanup();

    // Mesh ownership management with handles
    MeshHandle addMesh(eastl::unique_ptr<Mesh> mesh);
    void removeMesh(MeshHandle handle);
    Mesh* getMesh(MeshHandle handle);
    const Mesh* getMesh(MeshHandle handle) const;

    // Statistics
    size_t getMeshCount() const;

private:
    bool isValidHandle(MeshHandle handle) const;

    VulkanContext* context = nullptr;

    struct MeshSlot {
        eastl::unique_ptr<Mesh> mesh;
        uint32_t generation = 0;
        bool inUse = false;
    };

    eastl::vector<MeshSlot> meshSlots;
    eastl::vector<uint32_t> freeSlots;
    uint32_t nextSlot = 1;  // Start from 1, 0 is invalid
};

} // namespace violet
