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
    violet::Log::info("ResourceManager", "Pre-loading all shaders into ShaderLibrary...");

    // ============================================================================
    // TEMPORARY: Test new Slang auto-loading API (TODO: Replace old GLSL loading)
    // ============================================================================
    violet::Log::info("ResourceManager", "=== TESTING: loadSlangShader() API ===");

    // Test auto-loading PBR shader with automatic entry point detection
    auto pbrShaders = shaderLibrary->loadSlangShader(
        FileSystem::resolveRelativePath("shaders/slang/pbr_bindless.slang"));

    violet::Log::info("ResourceManager", "PBR shader auto-loaded {} shaders:", pbrShaders.size());
    for (const auto& shader : pbrShaders) {
        if (auto s = shader.lock()) {
            violet::Log::info("ResourceManager", "  - {}", s->getName().c_str());
        }
    }

    violet::Log::info("ResourceManager", "=== END TESTING ===");
    // ============================================================================

    using Language = Shader::Language;
    using Stage = Shader::Stage;

    // Shader definitions list
    struct ShaderDef {
        const char* name;
        const char* filePath;
        Stage stage;
    };

    eastl::vector<ShaderDef> shaders = {
        // Graphics shaders (vertex + fragment pairs)
        {"pbr_vert", "shaders/pbr_bindless.vert", Stage::Vertex},
        {"pbr_frag", "shaders/pbr_bindless.frag", Stage::Fragment},

        {"skybox_vert", "shaders/skybox.vert", Stage::Vertex},
        {"skybox_frag", "shaders/skybox.frag", Stage::Fragment},

        {"debug_vert", "shaders/debug.vert", Stage::Vertex},
        {"debug_frag", "shaders/debug.frag", Stage::Fragment},

        {"postprocess_vert", "shaders/postprocess.vert", Stage::Vertex},
        {"postprocess_frag", "shaders/postprocess.frag", Stage::Fragment},

        {"shadow_vert", "shaders/shadow.vert", Stage::Vertex},

        // Compute shaders
        {"equirect_to_cubemap", "shaders/equirect_to_cubemap.comp", Stage::Compute},
        {"irradiance_convolution", "shaders/irradiance_convolution.comp", Stage::Compute},
        {"prefilter_environment", "shaders/prefilter_environment.comp", Stage::Compute},
        {"brdf_lut", "shaders/brdf_lut.comp", Stage::Compute},
        {"luminance_average", "shaders/luminance_average.comp", Stage::Compute},
        {"luminance_histogram", "shaders/luminance_histogram.comp", Stage::Compute},
    };

    // Load all shaders
    for (const auto& shader : shaders) {
        shaderLibrary->load(shader.name, {
            .name = shader.name,
            .filePath = FileSystem::resolveRelativePath(shader.filePath),
            .entryPoint = "main",
            .stage = shader.stage,
            .language = Language::GLSL
        });
    }

    violet::Log::info("ResourceManager", "All {} shaders pre-loaded successfully", shaders.size());
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
