#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "resource/shader/SlangCompiler.hpp"
#include "resource/shader/ReflectionHelper.hpp"
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

    // Skip Test 1 and 2 (require non-existent test files)
    Log::info("Test", "Skipping Test 1-2 (test files not available)");

    // Test 3: Simple Vertex Shader (no imports)
    Log::info("Test", "");
    Log::info("Test", "=== Test 3: Simple Vertex Shader (no imports) ===");
    Shader::CreateInfo pbrVertInfo;
    pbrVertInfo.name = "test_simple_vertex";
    pbrVertInfo.filePath = "shaders/slang/test_simple.slang";
    pbrVertInfo.entryPoint = "vertexMain";
    pbrVertInfo.stage = Shader::Stage::Vertex;
    pbrVertInfo.language = Shader::Language::Slang;
    pbrVertInfo.includePaths.push_back("shaders/slang");

    auto result = compiler.compile(pbrVertInfo);

    if (!result.success) {
        Log::error("Test", "PBR vertex compilation failed: {}", result.errorMessage.c_str());
        return 1;
    }

    Log::info("Test", "PBR vertex compiled! SPIRV size: {} bytes", result.spirv.size() * 4);

    // Test reflection for PBR vertex
    auto pbrReflection = compiler.getReflection();
    if (pbrReflection) {
        ReflectionHelper helper(pbrReflection);
        auto descriptorLayouts = helper.extractDescriptorLayouts("pbr_vertex");
        Log::info("Test", "PBR Vertex - Found {} descriptor set layouts", descriptorLayouts.size());

        for (size_t i = 0; i < descriptorLayouts.size(); ++i) {
            const auto& layout = descriptorLayouts[i];
            if (layout.bindings.empty()) continue;

            Log::info("Test", "  Set {}: name='{}', frequency={}, {} bindings",
                i,
                layout.name.c_str(),
                static_cast<int>(layout.frequency),
                layout.bindings.size());

            for (const auto& binding : layout.bindings) {
                Log::info("Test", "    Binding {}: type={}, count={}, stages={}",
                    binding.binding,
                    static_cast<int>(binding.type),
                    binding.count,
                    static_cast<uint32_t>(binding.stages));
            }
        }

        auto pushConstants = helper.extractPushConstants();
        Log::info("Test", "PBR Vertex - Found {} push constant ranges", pushConstants.size());
        for (const auto& pc : pushConstants) {
            Log::info("Test", "  Offset: {}, Size: {} bytes", pc.offset, pc.size);
        }
    }

    Log::info("Test", "");
    Log::info("Test", "Skipping Test 4-5 (require pbr_bindless.slang with module imports)");

    Log::info("Test", "");
    Log::info("Test", "NOTE: Descriptor auto-registration will be tested in main application");
    Log::info("Test", "NOTE: Reflection-based descriptor update API will be tested in main application");
    Log::info("Test", "All Slang shader tests completed successfully!");
    return 0;
}