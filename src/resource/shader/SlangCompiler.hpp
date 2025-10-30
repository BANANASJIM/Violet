#pragma once

#include "ShaderCompiler.hpp"
#include <slang.h>
#include <slang-com-ptr.h>

namespace violet {

/**
 * @brief Slang shader compiler using Slang API
 *
 * Compiles Slang (.slang) files to SPIRV using the Slang compiler API.
 * Supports runtime compilation, hot reload, and shader reflection.
 */
class SlangCompiler : public ShaderCompiler {
public:
    SlangCompiler();
    ~SlangCompiler() override;

    CompileResult compile(const Shader::CreateInfo& info) override;
    bool hasSourceChanged(const eastl::string& filePath, size_t lastHash) const override;
    size_t computeSourceHash(const eastl::string& filePath) const override;

    /**
     * @brief Get all entry points from a Slang module
     * @param filePath Path to .slang module file
     * @param includePaths Search paths for imports
     * @return Vector of entry point info (name + stage), empty if failed
     */
    struct EntryPointInfo {
        eastl::string name;
        Shader::Stage stage;
    };
    eastl::vector<EntryPointInfo> getModuleEntryPoints(
        const eastl::string& filePath,
        const eastl::vector<eastl::string>& includePaths = {});

private:
    /**
     * @brief Convert Shader::Stage to Slang stage
     */
    static SlangStage stageToSlangStage(Shader::Stage stage);

    /**
     * @brief Check compilation diagnostics and extract errors
     */
    bool checkDiagnostics(slang::IBlob* diagnostics, CompileResult& result);

    /**
     * @brief Get or create session with given compilation settings
     * Reuses cached session if settings match, otherwise creates new one
     */
    slang::ISession* getOrCreateSession(
        const eastl::vector<eastl::string>& includePaths,
        const eastl::vector<eastl::string>& defines);

private:
    Slang::ComPtr<slang::IGlobalSession> globalSession;

    // Session cache (reuse session for identical compilation settings)
    Slang::ComPtr<slang::ISession> cachedSession;
    eastl::vector<eastl::string> cachedIncludePaths;
    eastl::vector<eastl::string> cachedDefines;
};

} // namespace violet