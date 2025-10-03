#pragma once

#include "resource/TextureManager.hpp"
#include "resource/MaterialManager.hpp"
#include "resource/MeshManager.hpp"

namespace violet {

class VulkanContext;
class DescriptorManager;

// Unified resource management facade
class ResourceManager {
public:
    ResourceManager() = default;
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // === Initialization ===
    void init(VulkanContext* ctx, DescriptorManager* descMgr, uint32_t maxFramesInFlight);
    void cleanup();

    // === Sub-manager Access ===
    TextureManager& getTextureManager() { return textureManager; }
    const TextureManager& getTextureManager() const { return textureManager; }

    MaterialManager& getMaterialManager() { return materialManager; }
    const MaterialManager& getMaterialManager() const { return materialManager; }

    MeshManager& getMeshManager() { return meshManager; }
    const MeshManager& getMeshManager() const { return meshManager; }

    // === Convenience Methods (delegates to sub-managers) ===
    void createDefaultResources();

private:
    VulkanContext* context = nullptr;

    // Sub-managers (order matters: initialization dependency)
    TextureManager textureManager;
    MaterialManager materialManager;  // depends on TextureManager
    MeshManager meshManager;
};

} // namespace violet
