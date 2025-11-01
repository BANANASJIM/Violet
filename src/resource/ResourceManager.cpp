#include "ResourceManager.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/PipelineBase.hpp"
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
    // Note: ShaderLibrary doesn't need DescriptorManager - layouts are registered by Pipeline
    shaderLibrary = eastl::make_unique<ShaderLibrary>(ctx);

    textureManager = eastl::make_unique<TextureManager>();
    textureManager->init(ctx, &descriptorManager);

    materialManager = eastl::make_unique<MaterialManager>();
    materialManager->init(ctx, &descriptorManager, textureManager.get(), shaderLibrary.get(), maxFramesInFlight);

    meshManager = eastl::make_unique<MeshManager>();
    meshManager->init(ctx);

    // 3. Pre-load all shaders
    loadAllShaders();

    // Note: Materials buffer initialization moved to ForwardRenderer::init()
    // This ensures PBRBindless material pipeline is created first, which registers
    // descriptor layouts with proper stage merging before materials buffer is allocated

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

// === ShaderResources Management ===

// DEPRECATED: Old API - use createShaderResources(name, pipeline, bufferName, frequency) instead
// This method created ShaderResources for all sets in a shader, which doesn't fit the new
// "one ShaderResources = one buffer" architecture

// Public wrapper: Create ShaderResources from buffer name (looks up buffer layout from pipeline)
eastl::shared_ptr<ShaderResources> ResourceManager::createShaderResources(
    const eastl::string& name,
    const PipelineBase* pipeline,
    const eastl::string& bufferName,
    UpdateFrequency frequency) {

    if (!pipeline) {
        violet::Log::error("ResourceManager", "Pipeline is null for ShaderResources '{}'", name.c_str());
        return nullptr;
    }

    const ShaderReflection* reflection = pipeline->getReflection();
    if (!reflection) {
        violet::Log::error("ResourceManager", "Pipeline has no reflection for ShaderResources '{}'", name.c_str());
        return nullptr;
    }

    // Find the resource by name to get set/binding information
    const ReflectedResource* resource = reflection->findResource(bufferName);
    if (!resource || resource->bufferLayoutIndex == ~0u) {
        violet::Log::error("ResourceManager", "Buffer '{}' not found in pipeline reflection", bufferName.c_str());
        return nullptr;
    }

    const ReflectedBuffer* bufferLayout = reflection->getBufferLayout(resource->bufferLayoutIndex);
    if (!bufferLayout) {
        violet::Log::error("ResourceManager", "Buffer layout not found for '{}'", bufferName.c_str());
        return nullptr;
    }

    return createShaderResourcesInternal(name, pipeline, bufferLayout,
                                        resource->set, resource->binding, frequency);
}

// Internal: Create ShaderResources from buffer layout
eastl::shared_ptr<ShaderResources> ResourceManager::createShaderResourcesInternal(
    const eastl::string& name,
    const PipelineBase* pipeline,
    const ReflectedBuffer* bufferLayout,
    uint32_t setIndex,
    uint32_t binding,
    UpdateFrequency frequency) {

    if (hasShaderResources(name)) {
        violet::Log::warn("ResourceManager", "ShaderResources '{}' already exists, returning existing instance", name.c_str());
        return shaderResourcesMap[name];
    }

    // Get resource info to determine descriptor type
    const ShaderReflection* reflection = pipeline->getReflection();
    const ReflectedResource* resourceInfo = reflection->findResourceByBinding(setIndex, binding);
    if (!resourceInfo) {
        violet::Log::error("ResourceManager", "No resource found for set {} binding {}",
                  setIndex, binding);
        return nullptr;
    }

    // Create ShaderResources - it handles all buffer creation internally
    auto resources = eastl::make_shared<ShaderResources>(
        name, bufferLayout, resourceInfo, setIndex, binding,
        resourceInfo->type, frequency,
        context, MAX_FRAMES_IN_FLIGHT
    );

    // Register
    shaderResourcesMap[name] = resources;

    if (frequency == UpdateFrequency::PerFrame) {
        perFrameResources.push_back(resources);
        violet::Log::debug("ResourceManager", "Registered '{}' for PerFrame synchronization", name.c_str());
    }

    return resources;
}

eastl::shared_ptr<ShaderResources> ResourceManager::getShaderResources(const eastl::string& name) {
    auto it = shaderResourcesMap.find(name);
    if (it != shaderResourcesMap.end()) {
        return it->second;
    }
    return nullptr;
}

const eastl::shared_ptr<ShaderResources> ResourceManager::getShaderResources(const eastl::string& name) const {
    auto it = shaderResourcesMap.find(name);
    if (it != shaderResourcesMap.end()) {
        return it->second;
    }
    return nullptr;
}

bool ResourceManager::hasShaderResources(const eastl::string& name) const {
    return shaderResourcesMap.find(name) != shaderResourcesMap.end();
}

// DEPRECATED: Descriptor sets are now managed by ShaderResourceBinding, not ShaderResources
// ShaderResources is now a pure buffer container and returns buffer bindings via getBufferBindings()

void ResourceManager::setCurrentFrame(uint32_t frameIndex) {
    // Synchronize DescriptorManager frame
    descriptorManager.setCurrentFrame(frameIndex);

    // Synchronize all PerFrame ShaderResources
    // Use iterator to handle weak_ptr invalidation
    for (auto it = perFrameResources.begin(); it != perFrameResources.end();) {
        if (auto res = it->lock()) {
            res->setCurrentFrame(frameIndex);
            ++it;
        } else {
            // Remove expired weak_ptr
            it = perFrameResources.erase(it);
        }
    }
}

} // namespace violet
