#include "Shader.hpp"
#include "core/Log.hpp"
#include "resource/shader/ShaderReflection.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"

namespace violet {

Shader::Shader(const CreateInfo& info, const eastl::vector<uint32_t>& spirv)
    : name(info.name)
    , filePath(info.filePath)
    , entryPoint(info.entryPoint)
    , stage(info.stage)
    , language(info.language)
    , spirvCode(spirv)
    , sourceHash(0)
    , includePaths(info.includePaths)
    , defines(info.defines) {
}

Shader::~Shader() {
    // extractedReflection (unique_ptr) is automatically cleaned up
}

void Shader::updateSPIRV(const eastl::vector<uint32_t>& spirv, size_t newHash) {
    spirvCode = spirv;
    sourceHash = newHash;
    Log::info("Shader", "Updated SPIRV for shader: {}", name.c_str());
}

vk::ShaderStageFlagBits Shader::stageToVkFlag(Stage stage) {
    switch (stage) {
        case Stage::Vertex:         return vk::ShaderStageFlagBits::eVertex;
        case Stage::Fragment:       return vk::ShaderStageFlagBits::eFragment;
        case Stage::Compute:        return vk::ShaderStageFlagBits::eCompute;
        case Stage::Geometry:       return vk::ShaderStageFlagBits::eGeometry;
        case Stage::TessControl:    return vk::ShaderStageFlagBits::eTessellationControl;
        case Stage::TessEvaluation: return vk::ShaderStageFlagBits::eTessellationEvaluation;
    }
    return vk::ShaderStageFlagBits::eVertex;
}

const char* Shader::stageToString(Stage stage) {
    switch (stage) {
        case Stage::Vertex:         return "Vertex";
        case Stage::Fragment:       return "Fragment";
        case Stage::Compute:        return "Compute";
        case Stage::Geometry:       return "Geometry";
        case Stage::TessControl:    return "TessControl";
        case Stage::TessEvaluation: return "TessEvaluation";
    }
    return "Unknown";
}

void Shader::setExtractedReflection(ShaderReflection&& reflection) {
    extractedReflection = eastl::make_unique<ShaderReflection>(eastl::move(reflection));
    Log::debug("Shader", "Extracted reflection data set for shader '{}'", name.c_str());
}

// Overload for backward compatibility - defaults to PerMaterial
void Shader::registerDescriptorLayouts(DescriptorManager* manager) {
    registerDescriptorLayouts(manager, UpdateFrequency::PerMaterial);
}

void Shader::registerDescriptorLayouts(DescriptorManager* manager, UpdateFrequency baseFrequency) {
    if (!extractedReflection) {
        Log::warn("Shader", "Shader '{}' has no extracted reflection data, cannot register layouts", name.c_str());
        return;
    }

    if (!manager) {
        Log::error("Shader", "DescriptorManager is null");
        return;
    }

    // Skip re-registration if already registered to avoid frequency conflicts
    // Different systems (Global vs Materials) will use the layouts registered by the first caller
    if (!descriptorLayoutHandles.empty()) {
        Log::debug("Shader", "Shader '{}' layouts already registered, skipping re-registration", name.c_str());
        return;
    }

    // Build descriptor layouts from extracted reflection (inline conversion)
    const auto& resourcesBySet = extractedReflection->getResourcesBySetMap();
    const auto& pushRanges = extractedReflection->getPushConstantRanges();

    // Early return only if BOTH descriptor layouts AND push constants are empty
    if (resourcesBySet.empty() && pushRanges.empty()) {
        Log::debug("Shader", "Shader '{}' has no descriptor layouts or push constants to register", name.c_str());
        return;
    }

    // Register descriptor layouts (if any)
    if (!resourcesBySet.empty()) {
        // Find maximum set index to determine vector size
        uint32_t maxSetIndex = 0;
        for (const auto& [setIndex, _] : resourcesBySet) {
            if (setIndex > maxSetIndex) maxSetIndex = setIndex;
        }

        // Create layouts vector (sparse, may have empty sets)
        eastl::vector<DescriptorLayoutDesc> layouts(maxSetIndex + 1);

        // Build layout for each set
        for (const auto& [setIndex, resourcesInSet] : resourcesBySet) {
        DescriptorLayoutDesc& layout = layouts[setIndex];

        // Set name
        char buffer[16];
        sprintf(buffer, "%u", setIndex);
        layout.name = name + "_set" + buffer;

        // Set frequency (caller can override default PerMaterial)
        layout.frequency = baseFrequency;

        // Convert resources to bindings
        for (const auto& resource : resourcesInSet) {
            BindingDesc binding;
            binding.binding = resource.binding;

            // Convert to Dynamic descriptor type for PerFrame buffers
            binding.type = resource.type;
            if (layout.frequency == UpdateFrequency::PerFrame) {
                if (resource.type == vk::DescriptorType::eUniformBuffer) {
                    binding.type = vk::DescriptorType::eUniformBufferDynamic;
                } else if (resource.type == vk::DescriptorType::eStorageBuffer) {
                    binding.type = vk::DescriptorType::eStorageBufferDynamic;
                }
            }

            binding.stages = resource.stages;
            binding.count = resource.arraySize;

            // Debug: Log binding details
            Log::debug("Shader", "  Set {} Binding {}: '{}' -> type={} ({})",
                      setIndex, resource.binding, resource.name.c_str(),
                      static_cast<uint32_t>(resource.type), vk::to_string(resource.type).c_str());

            // Bindless resources need special flags
            if (resource.isBindless) {
                binding.flags = vk::DescriptorBindingFlagBits::ePartiallyBound |
                               vk::DescriptorBindingFlagBits::eUpdateAfterBind;
                layout.isBindless = true;
                layout.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
            }

            layout.bindings.push_back(binding);
        }
        }

        // Register each layout and store handles
        // IMPORTANT: Preserve set index sparsity (e.g., [set0, empty, set2] → [handle0, 0, handle2])
        descriptorLayoutHandles.clear();
        descriptorLayoutHandles.resize(layouts.size(), 0);  // Initialize with 0 (no layout)

        for (size_t setIndex = 0; setIndex < layouts.size(); ++setIndex) {
            const auto& layout = layouts[setIndex];
            if (!layout.bindings.empty()) {
                LayoutHandle handle = manager->registerLayout(layout);
                descriptorLayoutHandles[setIndex] = handle;
            }
        }

        Log::info("Shader", "Registered {} descriptor layouts for shader '{}'",
                 descriptorLayoutHandles.size(), name.c_str());
    }

    // Register push constants from reflection (even if no descriptor layouts)
    if (!pushRanges.empty()) {
        PushConstantDesc pcDesc;
        pcDesc.ranges = pushRanges;
        pushConstantHandle = manager->registerPushConstants(pcDesc);
        Log::info("Shader", "Registered {} push constant range(s) for shader '{}'",
                 pushRanges.size(), name.c_str());
    } else {
        pushConstantHandle = 0;
    }
}

} // namespace violet