#pragma once

#include "Shader.hpp"
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace violet {

/**
 * @brief Base class for shader compilers (GLSL, Slang, etc.)
 *
 * Implementations:
 * - GLSLCompiler: Uses glslc to compile GLSL to SPIRV
 * - SlangCompiler: Uses Slang API to compile Slang to SPIRV
 */
class ShaderCompiler {
public:
    struct CompileResult {
        bool success = false;
        eastl::vector<uint32_t> spirv;
        eastl::string errorMessage;
        size_t sourceHash = 0;
    };

    virtual ~ShaderCompiler() = default;

    /**
     * @brief Compile shader source to SPIRV
     * @param info Shader creation info with source path and options
     * @return Compilation result with SPIRV code or error message
     */
    virtual CompileResult compile(const Shader::CreateInfo& info) = 0;

    /**
     * @brief Check if source file has changed since last compilation
     * @param filePath Path to shader source file
     * @param lastHash Hash from previous compilation
     * @return True if source has been modified
     */
    virtual bool hasSourceChanged(const eastl::string& filePath, size_t lastHash) const = 0;

    /**
     * @brief Compute hash of source file for cache invalidation
     */
    virtual size_t computeSourceHash(const eastl::string& filePath) const = 0;
};

} // namespace violet