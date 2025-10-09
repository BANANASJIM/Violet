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

private:
    /**
     * @brief Convert Shader::Stage to Slang stage
     */
    static SlangStage stageToSlangStage(Shader::Stage stage);

    /**
     * @brief Check compilation diagnostics and extract errors
     */
    bool checkDiagnostics(slang::IBlob* diagnostics, CompileResult& result);

private:
    Slang::ComPtr<slang::IGlobalSession> globalSession;
};

} // namespace violet