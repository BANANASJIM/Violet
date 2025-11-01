#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "resource/shader/SlangCompiler.hpp"
#include "resource/shader/ShaderReflection.hpp"
#include "resource/shader/ShaderLibrary.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "core/Log.hpp"
#include <iostream>
#include <cstdlib>

// EASTL operator new implementations
void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line) {
    return malloc(size);
}

void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line) {
    return aligned_alloc(alignment, size);
}

using namespace violet;

// Minimal Vulkan initialization helpers for Test 6
vk::raii::Instance createMinimalInstance(vk::raii::Context& context) {
    vk::ApplicationInfo appInfo(
        "SlangReflectionTest", 1,
        "VioletEngine", 1,
        VK_API_VERSION_1_3
    );

    vk::InstanceCreateInfo createInfo({}, &appInfo);
    return vk::raii::Instance(context, createInfo);
}

vk::raii::PhysicalDevice selectPhysicalDevice(vk::raii::Instance& instance) {
    auto devices = instance.enumeratePhysicalDevices();
    if (devices.empty()) {
        throw std::runtime_error("No Vulkan-compatible GPU found");
    }
    return std::move(devices[0]);  // Use first device
}

vk::raii::Device createDevice(vk::raii::PhysicalDevice& physicalDevice) {
    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo({}, 0, 1, &queuePriority);

    vk::DeviceCreateInfo createInfo({}, queueInfo);
    return vk::raii::Device(physicalDevice, createInfo);
}

int main() {
    Log::init();
    Log::info("Test", "Testing Slang compilation and reflection...");

    // Initialize compiler
    SlangCompiler compiler;

    // Skip Test 1-3 (test files not available or using deprecated ReflectionHelper)
    Log::info("Test", "Skipping Test 1-3 (deprecated or unavailable)");

    // Test 4: PBR Bindless Shader (with module imports)
    Log::info("Test", "");
    Log::info("Test", "=== Test 4: PBR Bindless Shader ===");
    Shader::CreateInfo pbrBindlessVertInfo;
    pbrBindlessVertInfo.name = "pbr_bindless_vertex";
    pbrBindlessVertInfo.filePath = "shaders/slang/pbr_bindless.slang";
    pbrBindlessVertInfo.entryPoint = "vertexMain";
    pbrBindlessVertInfo.stage = Shader::Stage::Vertex;
    pbrBindlessVertInfo.language = Shader::Language::Slang;
    pbrBindlessVertInfo.includePaths.push_back("shaders/slang");
    pbrBindlessVertInfo.includePaths.push_back("shaders");

    auto pbrBindlessResult = compiler.compile(pbrBindlessVertInfo);

    if (!pbrBindlessResult.shader) {
        Log::error("Test", "PBR bindless vertex compilation failed: {}", pbrBindlessResult.errorMessage.c_str());
        return 1;
    }

    Log::info("Test", "PBR bindless vertex compiled! SPIRV size: {} bytes",
             pbrBindlessResult.shader->getSPIRV().size() * 4);

    // Test reflection extraction from Shader object
    const ShaderReflection* reflectionPtr = pbrBindlessResult.shader->getShaderReflection();
    if (reflectionPtr) {
        Log::info("Test", "Got reflection from shader");

        const ShaderReflection& reflection = *reflectionPtr;

        Log::info("Test", "Reflection available!");

        uint32_t totalResources = 0;
        for (const auto& [set, resources] : reflection.getResourcesBySetMap()) {
            totalResources += resources.size();
        }

        // Count buffers (resources with bufferLayoutIndex)
        uint32_t bufferCount = 0;
        for (const auto& res : reflection.getAllResources()) {
            if (res.bufferLayoutIndex != ~0u) bufferCount++;
        }

        Log::info("Test", "Found {} total resources across all sets", totalResources);
        Log::info("Test", "Found {} buffers", bufferCount);

        for (const auto& [set, resources] : reflection.getResourcesBySetMap()) {
            Log::info("Test", "  Set {}:", set);
            for (const auto& res : resources) {
                Log::info("Test", "    Resource '{}': binding={}, type={}",
                         res.name.c_str(), res.binding, static_cast<int>(res.type));
            }
        }

        // Print buffer field details
        Log::info("Test", "");
        Log::info("Test", "Buffer Field Details:");
        for (const auto& res : reflection.getAllResources()) {
            if (res.bufferLayoutIndex != ~0u) {
                const auto* bufferPtr = reflection.getBufferLayout(res.bufferLayoutIndex);
                if (bufferPtr) {
                    const auto& buffer = *bufferPtr;
                    Log::info("Test", "  Buffer '{}' (set={}, binding={}, size={}B):",
                             res.name.c_str(), res.set, res.binding, buffer.totalSize);
                    for (const auto& field : buffer.fields) {
                        Log::info("Test", "    Field '{}': offset={}, size={}B, type={}",
                                 field.name.c_str(), field.offset, field.size, static_cast<int>(field.type));
                    }
                }
            }
        }
    }

    // Test 5: PBR Bindless Fragment Shader
    Log::info("Test", "");
    Log::info("Test", "=== Test 5: PBR Bindless Fragment Shader ===");
    Shader::CreateInfo pbrBindlessFragInfo;
    pbrBindlessFragInfo.name = "pbr_bindless_fragment";
    pbrBindlessFragInfo.filePath = "shaders/slang/pbr_bindless.slang";
    pbrBindlessFragInfo.entryPoint = "fragmentMain";
    pbrBindlessFragInfo.stage = Shader::Stage::Fragment;
    pbrBindlessFragInfo.language = Shader::Language::Slang;
    pbrBindlessFragInfo.includePaths.push_back("shaders/slang");
    pbrBindlessFragInfo.includePaths.push_back("shaders");

    auto pbrBindlessFragResult = compiler.compile(pbrBindlessFragInfo);

    if (!pbrBindlessFragResult.shader) {
        Log::error("Test", "PBR bindless fragment compilation failed: {}", pbrBindlessFragResult.errorMessage.c_str());
        return 1;
    }

    Log::info("Test", "PBR bindless fragment compiled! SPIRV size: {} bytes",
             pbrBindlessFragResult.shader->getSPIRV().size() * 4);

    // Test reflection extraction from Shader object
    const ShaderReflection* fragReflectionPtr = pbrBindlessFragResult.shader->getShaderReflection();
    if (fragReflectionPtr) {
        Log::info("Test", "Got reflection from shader");

        const ShaderReflection& fragReflection = *fragReflectionPtr;

        Log::info("Test", "Reflection available!");

        uint32_t totalResources = 0;
        for (const auto& [set, resources] : fragReflection.getResourcesBySetMap()) {
            totalResources += resources.size();
        }

        // Count buffers (resources with bufferLayoutIndex)
        uint32_t bufferCount = 0;
        for (const auto& res : fragReflection.getAllResources()) {
            if (res.bufferLayoutIndex != ~0u) bufferCount++;
        }

        Log::info("Test", "Found {} total resources across all sets", totalResources);
        Log::info("Test", "Found {} buffers", bufferCount);

        for (const auto& [set, resources] : fragReflection.getResourcesBySetMap()) {
            Log::info("Test", "  Set {}:", set);
            for (const auto& res : resources) {
                Log::info("Test", "    Resource '{}': binding={}, type={}",
                         res.name.c_str(), res.binding, static_cast<int>(res.type));
            }
        }

        // Print buffer field details
        Log::info("Test", "");
        Log::info("Test", "Buffer Field Details:");
        for (const auto& res : fragReflection.getAllResources()) {
            if (res.bufferLayoutIndex != ~0u) {
                const auto* bufferPtr = fragReflection.getBufferLayout(res.bufferLayoutIndex);
                if (bufferPtr) {
                    const auto& buffer = *bufferPtr;
                    Log::info("Test", "  Buffer '{}' (set={}, binding={}, size={}B):",
                             res.name.c_str(), res.set, res.binding, buffer.totalSize);
                    for (const auto& field : buffer.fields) {
                        Log::info("Test", "    Field '{}': offset={}, size={}B, type={}",
                                 field.name.c_str(), field.offset, field.size, static_cast<int>(field.type));
                    }
                }
            }
        }
    }

    // Test 6: Pointer Invalidation Bug Test
    Log::info("Test", "");
    Log::info("Test", "=== Test 6: Pointer Invalidation Bug Test ===");

    // Simulate the bug: Add multiple resources to trigger vector reallocation
    ShaderReflection testReflection;

    // Add first resource (e.g., sampler)
    ReflectedResource sampler;
    sampler.name = "texSampler";
    sampler.type = vk::DescriptorType::eSampler;
    sampler.set = 0;
    sampler.binding = 2;
    sampler.stages = vk::ShaderStageFlagBits::eCompute;
    testReflection.addResource(sampler);

    // Check initial lookup
    const auto* found1 = testReflection.findResource("texSampler");
    if (found1) {
        Log::info("Test", "Initial lookup: '{}' -> type={} (expected={})",
                 found1->name.c_str(), static_cast<uint32_t>(found1->type),
                 static_cast<uint32_t>(vk::DescriptorType::eSampler));
    }

    // Add second resource (e.g., texture)
    ReflectedResource texture;
    texture.name = "equirectangularMap";
    texture.type = vk::DescriptorType::eSampledImage;
    texture.set = 0;
    texture.binding = 0;
    texture.stages = vk::ShaderStageFlagBits::eCompute;
    testReflection.addResource(texture);

    // Add third resource to trigger potential reallocation
    ReflectedResource storageImage;
    storageImage.name = "outputCubemap";
    storageImage.type = vk::DescriptorType::eStorageImage;
    storageImage.set = 0;
    storageImage.binding = 1;
    storageImage.stages = vk::ShaderStageFlagBits::eCompute;
    testReflection.addResource(storageImage);

    // Now lookup all resources again
    Log::info("Test", "After adding 3 resources:");
    const auto* foundSampler = testReflection.findResource("texSampler");
    const auto* foundTexture = testReflection.findResource("equirectangularMap");
    const auto* foundStorage = testReflection.findResource("outputCubemap");

    bool testPassed = true;

    if (foundSampler) {
        bool correct = foundSampler->type == vk::DescriptorType::eSampler;
        Log::info("Test", "  texSampler: type={} (expected={}) {}",
                 static_cast<uint32_t>(foundSampler->type),
                 static_cast<uint32_t>(vk::DescriptorType::eSampler),
                 correct ? "✓" : "✗ WRONG!");
        testPassed &= correct;
    } else {
        Log::error("Test", "  texSampler: NOT FOUND ✗");
        testPassed = false;
    }

    if (foundTexture) {
        bool correct = foundTexture->type == vk::DescriptorType::eSampledImage;
        Log::info("Test", "  equirectangularMap: type={} (expected={}) {}",
                 static_cast<uint32_t>(foundTexture->type),
                 static_cast<uint32_t>(vk::DescriptorType::eSampledImage),
                 correct ? "✓" : "✗ WRONG!");
        testPassed &= correct;
    } else {
        Log::error("Test", "  equirectangularMap: NOT FOUND ✗");
        testPassed = false;
    }

    if (foundStorage) {
        bool correct = foundStorage->type == vk::DescriptorType::eStorageImage;
        Log::info("Test", "  outputCubemap: type={} (expected={}) {}",
                 static_cast<uint32_t>(foundStorage->type),
                 static_cast<uint32_t>(vk::DescriptorType::eStorageImage),
                 correct ? "✓" : "✗ WRONG!");
        testPassed &= correct;
    } else {
        Log::error("Test", "  outputCubemap: NOT FOUND ✗");
        testPassed = false;
    }

    if (!testPassed) {
        Log::error("Test", "Test 6 FAILED: Pointer invalidation bug detected!");
        return 1;
    }

    Log::info("Test", "Test 6 PASSED: All resources found correctly");

    Log::info("Test", "");
    Log::info("Test", "All Slang shader tests completed successfully!");
    return 0;
}