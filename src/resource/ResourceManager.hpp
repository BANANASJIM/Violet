#pragma once

#include "resource/TextureManager.hpp"
#include "resource/MaterialManager.hpp"
#include "resource/MeshManager.hpp"
#include "core/ThreadPool.hpp"

#include <EASTL/vector.h>
#include <EASTL/functional.h>
#include <EASTL/shared_ptr.h>
#include <mutex>
#include <atomic>

namespace violet {

class VulkanContext;
class DescriptorManager;

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

    // === Async Loading ===
    void submitAsyncTask(eastl::shared_ptr<AsyncLoadTask> task);
    void processAsyncTasks();  // Call every frame to process completed CPU work

private:
    VulkanContext* context = nullptr;

    // Sub-managers (order matters: initialization dependency)
    TextureManager textureManager;
    MaterialManager materialManager;  // depends on TextureManager
    MeshManager meshManager;

    // Async loading support
    ThreadPool threadPool;
    eastl::vector<eastl::shared_ptr<AsyncLoadTask>> pendingTasks;
    std::mutex taskMutex;
};

} // namespace violet
