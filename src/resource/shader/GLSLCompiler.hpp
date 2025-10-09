#pragma once

#include "ShaderCompiler.hpp"

namespace violet {

/**
 * @brief GLSL shader compiler using glslc (shaderc)
 *
 * Compiles GLSL (.vert, .frag, .comp, etc.) to SPIRV using glslc.
 * Supports offline compilation (via CMake) and runtime compilation.
 */
class GLSLCompiler : public ShaderCompiler {
public:
    GLSLCompiler();
    ~GLSLCompiler() override = default;

    CompileResult compile(const Shader::CreateInfo& info) override;
    bool hasSourceChanged(const eastl::string& filePath, size_t lastHash) const override;
    size_t computeSourceHash(const eastl::string& filePath) const override;

private:
    /**
     * @brief Convert Shader::Stage to glslc file extension
     */
    static const char* stageToExtension(Shader::Stage stage);

    /**
     * @brief Find glslc executable
     */
    eastl::string findGlslc() const;

    /**
     * @brief Load pre-compiled SPIRV from build directory
     */
    bool loadPrecompiledSPIRV(const eastl::string& filePath, CompileResult& result);

private:
    eastl::string glslcPath;
};

} // namespace violet