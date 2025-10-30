#pragma once

#include "Shader.hpp"
#include "ShaderReflection.hpp"
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/optional.h>
#include <EASTL/shared_ptr.h>

namespace violet {

/**
 * @brief Base class for shader compilers (Slang-based)
 *
 * Compile shader source to a complete Shader object with SPIRV and reflection.
 */
class ShaderCompiler {
public:
    struct CompileResult {
        eastl::shared_ptr<Shader> shader;  // Complete Shader object (or nullptr on failure)
        eastl::string errorMessage;        // Error details if compilation failed
    };

    virtual ~ShaderCompiler() = default;

    /**
     * @brief Compile shader source to complete Shader object
     * @param info Shader creation info with source path and options
     * @return Complete Shader with SPIRV + Reflection, or nullptr on failure
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