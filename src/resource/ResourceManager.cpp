#include "ResourceManager.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "resource/shader/ShaderLibrary.hpp"
#include "resource/TextureManager.hpp"
#include "resource/MaterialManager.hpp"
#include "resource/MeshManager.hpp"
#include "resource/Material.hpp"
#include "resource/Mesh.hpp"
#include "resource/Texture.hpp"

namespace violet {

ResourceManager::~ResourceManager() {
    cleanup();
}

void ResourceManager::init(VulkanContext* ctx, uint32_t maxFramesInFlight) {
    context = ctx;

    // 1. Initialize DescriptorManager first (base infrastructure)
    descriptorManager.init(context, maxFramesInFlight);

    // 2. Initialize sub-managers in dependency order using make_unique
    shaderLibrary = eastl::make_unique<ShaderLibrary>(ctx, &descriptorManager);

    textureManager = eastl::make_unique<TextureManager>();
    textureManager->init(ctx, &descriptorManager);

    materialManager = eastl::make_unique<MaterialManager>();
    materialManager->init(ctx, &descriptorManager, textureManager.get(), shaderLibrary.get(), maxFramesInFlight);

    meshManager = eastl::make_unique<MeshManager>();
    meshManager->init(ctx);

    // 3. Pre-load all shaders
    loadAllShaders();

    violet::Log::info("ResourceManager", "Initialized all sub-managers with DescriptorManager");
}

void ResourceManager::loadAllShaders() {
    violet::Log::info("ResourceManager", "Pre-loading all Slang shaders into ShaderLibrary...");

    // Slang shader modules to load (auto-detects all entry points via reflection)
    eastl::vector<const char*> slangShaders = {
        // Graphics shaders
        "shaders/slang/pbr_bindless.slang",    // vertexMain, fragmentMain
        "shaders/slang/skybox.slang",          // vertexMain, fragmentMain
        "shaders/slang/debug.slang",           // vertexMain, fragmentMain
        "shaders/slang/shadow.slang",          // vertexMain
        "shaders/slang/postprocess.slang",     // vertexMain, fragmentMain

        // Compute shaders (IBL)
        "shaders/slang/equirect_to_cubemap.slang",
        "shaders/slang/irradiance_convolution.slang",
        "shaders/slang/prefilter_environment.slang",
        "shaders/slang/brdf_lut.slang",

        // Compute shaders (Auto-exposure)
        "shaders/slang/luminance_histogram.slang",
        "shaders/slang/luminance_average.slang",
    };

    // Load all Slang shaders with automatic entry point detection
    int totalShaders = 0;
    for (const char* shaderPath : slangShaders) {
        auto shaders = shaderLibrary->loadSlangShader(FileSystem::resolveRelativePath(shaderPath));

        if (!shaders.empty()) {
            violet::Log::info("ResourceManager", "Loaded '{}' -> {} entry point(s):", shaderPath, shaders.size());
            for (const auto& shader : shaders) {
                if (auto s = shader.lock()) {
                    violet::Log::info("ResourceManager", "  - {}", s->getName().c_str());
                    totalShaders++;
                }
            }
        } else {
            violet::Log::error("ResourceManager", "Failed to load Slang shader: {}", shaderPath);
        }
    }

    violet::Log::info("ResourceManager", "All {} Slang shader(s) from {} module(s) pre-loaded successfully",
                     totalShaders, slangShaders.size());
}

void ResourceManager::cleanup() {
    // Cleanup in reverse dependency order
    if (meshManager) {
        meshManager->cleanup();
        meshManager.reset();
    }
    if (materialManager) {
        materialManager->cleanup();
        materialManager.reset();
    }
    if (textureManager) {
        textureManager->cleanup();
        textureManager.reset();
    }
    if (shaderLibrary) {
        shaderLibrary->clear();
        shaderLibrary.reset();
    }

    // Finally cleanup DescriptorManager (base infrastructure)
    descriptorManager.cleanup();

    violet::Log::info("ResourceManager", "Cleaned up all sub-managers including DescriptorManager");
}

void ResourceManager::createDefaultResources() {
    if (textureManager) {
        textureManager->createDefaultResources();
    }
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
