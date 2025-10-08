#include "ResourceManager.hpp"
#include "core/Log.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"

namespace violet {

ResourceManager::~ResourceManager() {
    cleanup();
}

void ResourceManager::init(VulkanContext* ctx, DescriptorManager* descMgr, uint32_t maxFramesInFlight) {
    context = ctx;

    // Initialize sub-managers in dependency order
    textureManager.init(ctx, descMgr);
    materialManager.init(ctx, descMgr, &textureManager, maxFramesInFlight);
    meshManager.init(ctx);

    violet::Log::info("ResourceManager", "Initialized all sub-managers");
}

void ResourceManager::cleanup() {
    // Cleanup in reverse dependency order
    meshManager.cleanup();
    materialManager.cleanup();
    textureManager.cleanup();

    violet::Log::info("ResourceManager", "Cleaned up all sub-managers");
}

void ResourceManager::createDefaultResources() {
    textureManager.createDefaultResources();
}

void ResourceManager::submitAsyncTask(eastl::shared_ptr<AsyncLoadTask> task) {
    // Submit CPU work to thread pool
    threadPool.submit([task]() {
        if (task->cpuWork) {
            task->cpuWork();
        }
        task->cpuReady = true;
    });

    // Add to pending tasks for main thread processing
    {
        std::lock_guard<std::mutex> lock(taskMutex);
        pendingTasks.push_back(task);
    }
}

void ResourceManager::processAsyncTasks() {
    eastl::vector<eastl::shared_ptr<AsyncLoadTask>> completedTasks;

    // Find completed tasks
    {
        std::lock_guard<std::mutex> lock(taskMutex);
        for (auto it = pendingTasks.begin(); it != pendingTasks.end();) {
            if ((*it)->cpuReady) {
                completedTasks.push_back(*it);
                it = pendingTasks.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Execute main thread work for completed tasks
    for (auto& task : completedTasks) {
        if (task->mainThreadWork) {
            task->mainThreadWork();
        }
    }
}

} // namespace violet
