#pragma once

#include "resource/TextureManager.hpp"
#include "resource/MaterialManager.hpp"
#include "resource/MeshManager.hpp"
#include "resource/shader/ShaderLibrary.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "core/ThreadPool.hpp"

#include <EASTL/vector.h>
#include <EASTL/functional.h>
#include <EASTL/shared_ptr.h>
#include <mutex>
#include <atomic>

namespace violet {

class VulkanContext;

// Async loading task for CPU/GPU work separation
struct AsyncLoadTask {
    eastl::function<void()> cpuWork;         // Work thread: file IO, parsing, decoding
    eastl::function<void()> mainThreadWork;  // Main thread: GPU resource creation, callback
    std::atomic<bool> cpuReady{false};

    AsyncLoadTask(eastl::function<void()> cpu, eastl::function<void()> main)
        : cpuWork(eastl::move(cpu)), mainThreadWork(eastl::move(main)) {}
};

// Unified resource management facade
class ResourceManager {
public:
    ResourceManager() = default;
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // === Initialization ===
    void init(VulkanContext* ctx, uint32_t maxFramesInFlight);
    void cleanup();

    // === Sub-manager Access ===
    ShaderLibrary* getShaderLibrary() { return shaderLibrary.get(); }
    const ShaderLibrary* getShaderLibrary() const { return shaderLibrary.get(); }

    TextureManager* getTextureManager() { return textureManager.get(); }
    const TextureManager* getTextureManager() const { return textureManager.get(); }

    MaterialManager* getMaterialManager() { return materialManager.get(); }
    const MaterialManager* getMaterialManager() const { return materialManager.get(); }

    MeshManager* getMeshManager() { return meshManager.get(); }
    const MeshManager* getMeshManager() const { return meshManager.get(); }

    DescriptorManager& getDescriptorManager() { return descriptorManager; }
    const DescriptorManager& getDescriptorManager() const { return descriptorManager; }

    // === Convenience Methods (delegates to sub-managers) ===
    void createDefaultResources();

    // === Async Loading ===
    void submitAsyncTask(eastl::shared_ptr<AsyncLoadTask> task);
    void processAsyncTasks();  // Call every frame to process completed CPU work

private:
    void loadAllShaders();  // Pre-load all shaders into ShaderLibrary
    VulkanContext* context = nullptr;

    // Sub-managers (order matters: initialization dependency)
    DescriptorManager descriptorManager;                 // Base infrastructure (owned)
    eastl::unique_ptr<ShaderLibrary> shaderLibrary;      // no dependencies
    eastl::unique_ptr<TextureManager> textureManager;    // depends on DescriptorManager
    eastl::unique_ptr<MaterialManager> materialManager;  // depends on TextureManager + DescriptorManager
    eastl::unique_ptr<MeshManager> meshManager;

    // Async loading support
    ThreadPool threadPool;
    eastl::vector<eastl::shared_ptr<AsyncLoadTask>> pendingTasks;
    std::mutex taskMutex;
};

} // namespace violet
