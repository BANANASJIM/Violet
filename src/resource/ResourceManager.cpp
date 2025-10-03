#include "ResourceManager.hpp"
#include "core/Log.hpp"
#include "renderer/core/VulkanContext.hpp"
#include "renderer/descriptor/DescriptorManager.hpp"

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

} // namespace violet
