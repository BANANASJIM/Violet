#include "Shader.hpp"
#include "core/Log.hpp"
#include "resource/shader/ReflectionHelper.hpp"
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

void Shader::setReflection(slang::ProgramLayout* layout) {
    reflection = layout;
    if (reflection) {
        Log::debug("Shader", "Reflection data set for shader '{}'", name.c_str());
    }
}

void Shader::registerDescriptorLayouts(DescriptorManager* manager) {
    if (!reflection) {
        Log::warn("Shader", "Shader '{}' has no reflection data, cannot register layouts", name.c_str());
        return;
    }

    if (!manager) {
        Log::error("Shader", "DescriptorManager is null");
        return;
    }

    // Use ReflectionHelper to extract layouts
    ReflectionHelper helper(reflection);
    auto layouts = helper.extractDescriptorLayouts(name);

    if (layouts.empty()) {
        Log::debug("Shader", "Shader '{}' has no descriptor layouts to register", name.c_str());
        return;
    }

    // Extract ShaderReflection (field-level metadata for UBO/SSBO)
    ShaderReflection shaderReflection;
    bool hasFieldReflection = extractReflection(reflection, shaderReflection);

    // Register each layout and store handles
    // IMPORTANT: Preserve set index sparsity (e.g., [set0, empty, set2] → [handle0, 0, handle2])
    descriptorLayoutHandles.clear();
    descriptorLayoutHandles.resize(layouts.size(), 0);  // Initialize with 0 (no layout)

    for (size_t setIndex = 0; setIndex < layouts.size(); ++setIndex) {
        const auto& layout = layouts[setIndex];
        if (!layout.bindings.empty()) {
            LayoutHandle handle = manager->registerLayout(layout);
            descriptorLayoutHandles[setIndex] = handle;

            // Store reflection data if available (for dynamic UBO updates)
            if (hasFieldReflection) {
                manager->setReflection(handle, shaderReflection);
                Log::debug("Shader", "Stored reflection data for set {} layout '{}' (handle={})",
                          setIndex, layout.name.c_str(), handle);
            }
        }
    }

    Log::info("Shader", "Registered {} descriptor layouts for shader '{}' (reflection: {})",
             descriptorLayoutHandles.size(), name.c_str(), hasFieldReflection ? "yes" : "no");

    // Register push constants
    auto pushConstants = helper.extractPushConstants();
    if (!pushConstants.empty()) {
        PushConstantDesc desc;
        for (const auto& pc : pushConstants) {
            desc.ranges.push_back(vk::PushConstantRange{pc.stageFlags, pc.offset, pc.size});
        }
        pushConstantHandle = manager->registerPushConstants(desc);
        Log::info("Shader", "Registered push constants for shader '{}' (handle={})",
                 name.c_str(), pushConstantHandle);
    } else {
        pushConstantHandle = 0;  // No push constants
    }
}

} // namespace violet