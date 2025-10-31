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

eastl::shared_ptr<ShaderResources> ResourceManager::createShaderResources(
    const eastl::string& name,
    const eastl::string& shaderName,
    UpdateFrequency frequency,
    const eastl::unordered_map<uint32_t, uint32_t>& bufferSizeOverrides) {

    if (hasShaderResources(name)) {
        violet::Log::warn("ResourceManager", "ShaderResources '{}' already exists, returning existing instance", name.c_str());
        return shaderResourcesMap[name];
    }

    auto shaderWeakPtr = shaderLibrary->get(shaderName);
    auto shader = shaderWeakPtr.lock();
    if (!shader) {
        violet::Log::error("ResourceManager", "Failed to find shader '{}' for ShaderResources '{}'",
                          shaderName.c_str(), name.c_str());
        return nullptr;
    }

    if (!shader->hasReflection()) {
        violet::Log::error("ResourceManager", "Shader '{}' has no reflection data", shaderName.c_str());
        return nullptr;
    }

    const ShaderReflection* shaderReflection = shader->getShaderReflection();
    if (!shaderReflection) {
        violet::Log::error("ResourceManager", "Shader '{}' has no extracted reflection data", shaderName.c_str());
        return nullptr;
    }

    // Register descriptor layouts from shader reflection (if not already registered)
    shader->registerDescriptorLayouts(&descriptorManager);

    const auto& layoutHandles = shader->getDescriptorLayoutHandles();
    if (layoutHandles.empty()) {
        violet::Log::warn("ResourceManager", "Shader '{}' has no descriptor layouts", shaderName.c_str());
    }

    // Copy reflection and extract resourcesBySet BEFORE moving
    ShaderReflection reflectionCopy = *shaderReflection;
    // Make a COPY of the map, not a reference, so it survives the move below
    auto resourcesBySet = reflectionCopy.getResourcesBySetMap();

    // Create ShaderResources with DescriptorManager (moves reflectionCopy)
    auto resources = eastl::make_shared<ShaderResources>(
        name, shader, eastl::move(reflectionCopy), context, 3, &descriptorManager
    );

    // Create descriptor sets and buffers for each set
    for (size_t setIndex = 0; setIndex < layoutHandles.size(); ++setIndex) {
        LayoutHandle layoutHandle = layoutHandles[setIndex];

        if (layoutHandle == 0) {
            continue;
        }

        auto setResIt = resourcesBySet.find(static_cast<uint32_t>(setIndex));
        if (setResIt == resourcesBySet.end() || setResIt->second.empty()) {
            continue;
        }

        const auto& setResources = setResIt->second;

        bool isBindless = false;
        for (const auto& res : setResources) {
            if (res.isBindless) {
                isBindless = true;
                break;
            }
        }

        if (!descriptorManager.hasLayout(layoutHandle)) {
            violet::Log::error("ResourceManager", "Layout handle {} not registered", layoutHandle);
            continue;
        }

        // Use the frequency parameter passed by the caller
        ShaderResources::SetData setData;
        setData.setIndex = static_cast<uint32_t>(setIndex);
        setData.layoutHandle = layoutHandle;
        setData.isBindless = isBindless;
        setData.frequency = frequency;
        setData.hasBuffer = false;
        setData.alignedSize = 0;
        setData.mappedData = nullptr;

        if (isBindless) {
            setData.descriptorSet = descriptorManager.getBindlessSet();
            violet::Log::debug("ResourceManager", "Instance '{}' using global bindless set for set {}",
                      name.c_str(), setIndex);
        } else {
            setData.descriptorSet = descriptorManager.allocateSet(layoutHandle);

            if (!setData.descriptorSet) {
                violet::Log::error("ResourceManager", "Failed to allocate descriptor set for set {}", setIndex);
                continue;
            }

            // Count buffer bindings in this set
            eastl::vector<const ReflectedResource*> bufferBindings;
            for (const auto& res : setResources) {
                if (res.type == vk::DescriptorType::eUniformBuffer ||
                    res.type == vk::DescriptorType::eStorageBuffer) {
                    bufferBindings.push_back(&res);
                }
            }

            if (bufferBindings.empty()) {
                // No buffers in this set
                setData.hasBuffer = false;
            } else if (bufferBindings.size() == 1) {
                // Single buffer binding - use legacy single buffer approach
                const auto& res = *bufferBindings[0];
                setData.hasBuffer = true;

                uint32_t totalBufferSize = 0;
                if (res.bufferLayoutIndex != ~0u) {
                    const auto* bufferLayout = shaderReflection->getBufferLayout(res.bufferLayoutIndex);
                    if (bufferLayout) {
                        totalBufferSize = bufferLayout->totalSize;
                    }
                }

                // Check for buffer size override
                auto overrideIt = bufferSizeOverrides.find(static_cast<uint32_t>(setIndex));
                if (overrideIt != bufferSizeOverrides.end()) {
                    totalBufferSize = overrideIt->second;
                    violet::Log::debug("ResourceManager", "Using buffer size override for set {}: {} bytes", setIndex, totalBufferSize);
                }

                uint32_t bufferSize = totalBufferSize;
                setData.alignedSize = totalBufferSize;

                if (frequency == UpdateFrequency::PerFrame) {
                    vk::PhysicalDeviceProperties props = context->getPhysicalDevice().getProperties();
                    uint32_t minAlignment = static_cast<uint32_t>(props.limits.minUniformBufferOffsetAlignment);
                    setData.alignedSize = (totalBufferSize + minAlignment - 1) & ~(minAlignment - 1);
                    bufferSize = setData.alignedSize * 3;
                }

                char setIndexStr[16];
                snprintf(setIndexStr, sizeof(setIndexStr), "%u", static_cast<uint32_t>(setIndex));
                eastl::string debugName = name + "_set" + setIndexStr;

                BufferInfo bufferInfo{
                    .size = bufferSize,
                    .usage = vk::BufferUsageFlagBits::eUniformBuffer |
                             vk::BufferUsageFlagBits::eStorageBuffer,
                    .memoryUsage = MemoryUsage::CPU_TO_GPU,
                    .debugName = debugName
                };

                setData.buffer = ResourceFactory::createBuffer(context, bufferInfo);
                setData.mappedData = setData.buffer.mappedData;

                vk::DeviceSize range = frequency == UpdateFrequency::PerFrame ?
                                       setData.alignedSize : totalBufferSize;

                // Convert to Dynamic descriptor type for PerFrame buffers
                vk::DescriptorType descriptorType = res.type;
                if (frequency == UpdateFrequency::PerFrame) {
                    if (res.type == vk::DescriptorType::eUniformBuffer) {
                        descriptorType = vk::DescriptorType::eUniformBufferDynamic;
                    } else if (res.type == vk::DescriptorType::eStorageBuffer) {
                        descriptorType = vk::DescriptorType::eStorageBufferDynamic;
                    }
                }

                descriptorManager.bindBuffer(setData.descriptorSet, res.binding,
                                            setData.buffer, descriptorType,
                                            0, range);

                violet::Log::debug("ResourceManager", "Allocated single buffer ({}B total, {}B per frame) for set {} binding {} in '{}'",
                          bufferSize, setData.alignedSize, setIndex, res.binding, name.c_str());
            } else {
                // Multiple buffer bindings - create independent buffer for each binding
                setData.hasBuffer = true;

                for (const auto* resPtr : bufferBindings) {
                    const auto& res = *resPtr;

                    uint32_t elementSize = 0;
                    if (res.bufferLayoutIndex != ~0u) {
                        const auto* bufferLayout = shaderReflection->getBufferLayout(res.bufferLayoutIndex);
                        if (bufferLayout) {
                            elementSize = bufferLayout->totalSize;
                        }
                    }

                    // For StructuredBuffer, default to 64 elements (can be overridden)
                    uint32_t elementCount = (res.type == vk::DescriptorType::eStorageBuffer) ? 64 : 1;
                    uint32_t totalBufferSize = elementSize * elementCount;

                    // Check for per-binding size override (format: setIndex * 1000 + binding)
                    uint32_t overrideKey = static_cast<uint32_t>(setIndex) * 1000 + res.binding;
                    auto overrideIt = bufferSizeOverrides.find(overrideKey);
                    if (overrideIt != bufferSizeOverrides.end()) {
                        totalBufferSize = overrideIt->second;
                        violet::Log::debug("ResourceManager", "Using buffer size override for set {} binding {}: {} bytes",
                                  setIndex, res.binding, totalBufferSize);
                    }

                    ShaderResources::BindingBufferData bindingData;
                    bindingData.elementSize = elementSize;
                    bindingData.alignedSize = totalBufferSize;

                    uint32_t bufferSize = totalBufferSize;
                    if (frequency == UpdateFrequency::PerFrame) {
                        vk::PhysicalDeviceProperties props = context->getPhysicalDevice().getProperties();
                        uint32_t minAlignment = static_cast<uint32_t>(props.limits.minUniformBufferOffsetAlignment);
                        bindingData.alignedSize = (totalBufferSize + minAlignment - 1) & ~(minAlignment - 1);
                        bufferSize = bindingData.alignedSize * 3;
                    }

                    char bindingStr[32];
                    snprintf(bindingStr, sizeof(bindingStr), "%u_binding%u",
                            static_cast<uint32_t>(setIndex), res.binding);
                    eastl::string debugName = name + "_set" + bindingStr;

                    BufferInfo bufferInfo{
                        .size = bufferSize,
                        .usage = vk::BufferUsageFlagBits::eUniformBuffer |
                                 vk::BufferUsageFlagBits::eStorageBuffer,
                        .memoryUsage = MemoryUsage::CPU_TO_GPU,
                        .debugName = debugName
                    };

                    bindingData.buffer = ResourceFactory::createBuffer(context, bufferInfo);
                    bindingData.mappedData = bindingData.buffer.mappedData;

                    vk::DeviceSize range = frequency == UpdateFrequency::PerFrame ?
                                           bindingData.alignedSize : totalBufferSize;

                    // Convert to Dynamic descriptor type for PerFrame buffers
                    vk::DescriptorType descriptorType = res.type;
                    if (frequency == UpdateFrequency::PerFrame) {
                        if (res.type == vk::DescriptorType::eUniformBuffer) {
                            descriptorType = vk::DescriptorType::eUniformBufferDynamic;
                        } else if (res.type == vk::DescriptorType::eStorageBuffer) {
                            descriptorType = vk::DescriptorType::eStorageBufferDynamic;
                        }
                    }

                    descriptorManager.bindBuffer(setData.descriptorSet, res.binding,
                                                bindingData.buffer, descriptorType,
                                                0, range);

                    setData.buffersByBinding[res.binding] = eastl::move(bindingData);

                    violet::Log::debug("ResourceManager", "Allocated buffer ({}B total, {}B per frame, element size {}B) for set {} binding {} in '{}'",
                              bufferSize, bindingData.alignedSize, elementSize, setIndex, res.binding, name.c_str());
                }
            }
        }

        resources->sets[static_cast<uint32_t>(setIndex)] = eastl::move(setData);
    }

    shaderResourcesMap[name] = resources;

    // Track PerFrame ShaderResources for frame synchronization
    if (frequency == UpdateFrequency::PerFrame) {
        perFrameResources.push_back(resources);
        violet::Log::debug("ResourceManager", "Registered '{}' for PerFrame synchronization", name.c_str());
    }

    violet::Log::info("ResourceManager", "Created ShaderResources '{}' from shader '{}' ({} sets)",
              name.c_str(), shaderName.c_str(), resources->sets.size());

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

vk::DescriptorSet ResourceManager::getDescriptorSet(const eastl::string& resourcesName, uint32_t setIndex) const {
    auto it = shaderResourcesMap.find(resourcesName);
    if (it == shaderResourcesMap.end()) {
        violet::Log::error("ResourceManager",
            "ShaderResources '{}' not found when getting descriptor set", resourcesName.c_str());
        return nullptr;
    }

    const auto& resources = it->second;
    if (!resources) {
        violet::Log::error("ResourceManager",
            "ShaderResources '{}' is null", resourcesName.c_str());
        return nullptr;
    }

    return resources->getSet(setIndex);
}

eastl::vector<uint32_t> ResourceManager::getDynamicOffsets(const eastl::string& resourcesName, uint32_t setIndex) const {
    auto it = shaderResourcesMap.find(resourcesName);
    if (it == shaderResourcesMap.end()) {
        violet::Log::error("ResourceManager",
            "ShaderResources '{}' not found when getting dynamic offsets", resourcesName.c_str());
        return {};
    }

    const auto& resources = it->second;
    if (!resources) {
        violet::Log::error("ResourceManager",
            "ShaderResources '{}' is null", resourcesName.c_str());
        return {};
    }

    return resources->getDynamicOffsetsForSet(setIndex);
}

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
