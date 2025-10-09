#include "Shader.hpp"
#include "core/Log.hpp"

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

} // namespace violet