#include "SlangCompiler.hpp"
#include "ShaderReflection.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include <sys/stat.h>

namespace violet {

SlangCompiler::SlangCompiler() {
    // Create Slang global session
    if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef()))) {
        Log::error("SlangCompiler", "Failed to create Slang global session");
        return;
    }

    Log::info("SlangCompiler", "Initialized Slang compiler");
}

SlangCompiler::~SlangCompiler() {
    cachedSession = nullptr;
    globalSession = nullptr;
}

ShaderCompiler::CompileResult SlangCompiler::compile(const Shader::CreateInfo& info) {
    CompileResult result;

    if (!globalSession) {
        result.errorMessage = "Slang global session not initialized";
        return result;
    }

    // Get or create cached session (reuses session if settings match)
    slang::ISession* session = getOrCreateSession(info.includePaths, info.defines);
    if (!session) {
        result.errorMessage = "Failed to create/get Slang session";
        return result;
    }

    // Load module (Slang will auto-load imported modules like Common, PBR, Sampling)
    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = session->loadModule(info.filePath.c_str(), diagnostics.writeRef());

    if (!module) {
        checkDiagnostics(diagnostics, result);
        return result;
    }

    // Find entry point
    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    if (SLANG_FAILED(module->findEntryPointByName(info.entryPoint.c_str(), entryPoint.writeRef()))) {
        result.errorMessage = "Entry point '" + info.entryPoint + "' not found";
        return result;
    }

    // Create component list
    slang::IComponentType* components[] = {module, entryPoint};
    Slang::ComPtr<slang::IComponentType> program;
    if (SLANG_FAILED(session->createCompositeComponentType(
            components, 2, program.writeRef(), diagnostics.writeRef()))) {
        checkDiagnostics(diagnostics, result);
        return result;
    }

    // Link program
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    if (SLANG_FAILED(program->link(linkedProgram.writeRef(), diagnostics.writeRef()))) {
        checkDiagnostics(diagnostics, result);
        return result;
    }

    // Get SPIRV code
    Slang::ComPtr<slang::IBlob> spirvCode;
    if (SLANG_FAILED(linkedProgram->getEntryPointCode(
            0, 0, spirvCode.writeRef(), diagnostics.writeRef()))) {
        checkDiagnostics(diagnostics, result);
        return result;
    }

    // Copy SPIRV
    const uint32_t* spirvData = reinterpret_cast<const uint32_t*>(spirvCode->getBufferPointer());
    size_t spirvSize = spirvCode->getBufferSize() / sizeof(uint32_t);
    eastl::vector<uint32_t> spirv(spirvSize);
    memcpy(spirv.data(), spirvData, spirvCode->getBufferSize());

    // CRITICAL: Extract reflection data IMMEDIATELY before linkedProgram goes out of scope!
    slang::ProgramLayout* programLayout = linkedProgram->getLayout();
    if (!programLayout) {
        result.errorMessage = "Failed to get program layout for reflection";
        return result;
    }

    ShaderReflection reflection;
    if (!extractReflection(static_cast<void*>(programLayout), reflection)) {
        result.errorMessage = "Failed to extract reflection data";
        return result;
    }

    // Count buffers for logging
    uint32_t bufferCount = 0;
    for (const auto& res : reflection.getAllResources()) {
        if (res.bufferLayoutIndex != ~0u) bufferCount++;
    }

    Log::debug("SlangCompiler", "Extracted reflection: {} buffers, {} resources total",
              bufferCount, reflection.getAllResources().size());

    // Create complete Shader object with SPIRV + Reflection
    auto shader = eastl::make_shared<Shader>(info, spirv);
    size_t sourceHash = computeSourceHash(info.filePath);
    shader->updateSPIRV(spirv, sourceHash);
    shader->setExtractedReflection(eastl::move(reflection));

    result.shader = shader;
    result.errorMessage = "";  // Success

    Log::debug("SlangCompiler", "Successfully compiled Shader '{}' ({} bytes SPIRV)",
               info.name.c_str(), spirv.size() * 4);

    return result;
}

bool SlangCompiler::hasSourceChanged(const eastl::string& filePath, size_t lastHash) const {
    size_t currentHash = computeSourceHash(filePath);
    return currentHash != lastHash && currentHash != 0;
}

size_t SlangCompiler::computeSourceHash(const eastl::string& filePath) const {
    struct stat fileInfo;
    if (stat(filePath.c_str(), &fileInfo) != 0) {
        return 0;
    }

    // Simple hash: combine file size and modification time
    size_t hash = fileInfo.st_size;
    hash ^= fileInfo.st_mtime + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    return hash;
}

SlangStage SlangCompiler::stageToSlangStage(Shader::Stage stage) {
    switch (stage) {
        case Shader::Stage::Vertex:         return SLANG_STAGE_VERTEX;
        case Shader::Stage::Fragment:       return SLANG_STAGE_FRAGMENT;
        case Shader::Stage::Compute:        return SLANG_STAGE_COMPUTE;
        case Shader::Stage::Geometry:       return SLANG_STAGE_GEOMETRY;
        case Shader::Stage::TessControl:    return SLANG_STAGE_HULL;
        case Shader::Stage::TessEvaluation: return SLANG_STAGE_DOMAIN;
    }
    return SLANG_STAGE_NONE;
}

static Shader::Stage slangStageToShaderStage(SlangStage slangStage) {
    switch (slangStage) {
        case SLANG_STAGE_VERTEX:   return Shader::Stage::Vertex;
        case SLANG_STAGE_FRAGMENT: return Shader::Stage::Fragment;
        case SLANG_STAGE_COMPUTE:  return Shader::Stage::Compute;
        case SLANG_STAGE_GEOMETRY: return Shader::Stage::Geometry;
        case SLANG_STAGE_HULL:     return Shader::Stage::TessControl;
        case SLANG_STAGE_DOMAIN:   return Shader::Stage::TessEvaluation;
        default:                   return Shader::Stage::Vertex; // Fallback
    }
}

eastl::vector<SlangCompiler::EntryPointInfo> SlangCompiler::getModuleEntryPoints(
    const eastl::string& filePath,
    const eastl::vector<eastl::string>& includePaths) {

    eastl::vector<EntryPointInfo> entryPoints;

    if (!globalSession) {
        Log::error("SlangCompiler", "Slang global session not initialized");
        return entryPoints;
    }

    // Get or create cached session (no defines for entry point enumeration)
    eastl::vector<eastl::string> emptyDefines;
    slang::ISession* session = getOrCreateSession(includePaths, emptyDefines);
    if (!session) {
        Log::error("SlangCompiler", "Failed to create/get Slang session for entry point enumeration");
        return entryPoints;
    }

    // Load module
    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = session->loadModule(filePath.c_str(), diagnostics.writeRef());

    if (!module) {
        Log::error("SlangCompiler", "Failed to load module '{}' for entry point enumeration", filePath.c_str());
        // Print diagnostics if available
        if (diagnostics && diagnostics->getBufferSize() > 0) {
            const char* diagText = reinterpret_cast<const char*>(diagnostics->getBufferPointer());
            Log::error("SlangCompiler", "Module load diagnostics:\n{}", diagText);
        }
        return entryPoints;
    }

    // Get entry point count
    SlangInt32 entryPointCount = module->getDefinedEntryPointCount();
    Log::info("SlangCompiler", "Module '{}' has {} entry points", filePath.c_str(), entryPointCount);

    // Enumerate all entry points
    for (SlangInt32 i = 0; i < entryPointCount; ++i) {
        Slang::ComPtr<slang::IEntryPoint> entryPoint;
        if (SLANG_FAILED(module->getDefinedEntryPoint(i, entryPoint.writeRef()))) {
            continue;
        }

        // Get entry point reflection
        slang::EntryPointReflection* entryPointRefl = entryPoint->getLayout()->getEntryPointByIndex(0);
        if (!entryPointRefl) continue;

        // Get entry point name
        const char* entryPointName = entryPointRefl->getName();

        // Get entry point stage
        SlangStage slangStage = entryPointRefl->getStage();

        EntryPointInfo info;
        info.name = entryPointName;
        info.stage = slangStageToShaderStage(slangStage);

        entryPoints.push_back(info);

        const char* stageNames[] = {"Vertex", "Fragment", "Compute", "Geometry", "TessControl", "TessEvaluation"};
        Log::info("SlangCompiler", "  Entry point {}: '{}' ({})",
                 i, entryPointName, stageNames[static_cast<int>(info.stage)]);
    }

    return entryPoints;
}

slang::ISession* SlangCompiler::getOrCreateSession(
    const eastl::vector<eastl::string>& includePaths,
    const eastl::vector<eastl::string>& defines) {

    // Check if we can reuse cached session
    bool canReuseSession = cachedSession != nullptr &&
                          cachedIncludePaths == includePaths &&
                          cachedDefines == defines;

    if (canReuseSession) {
        return cachedSession.get();
    }

    // Need to create new session
    Log::debug("SlangCompiler", "Creating new session (includePaths changed: {}, defines changed: {})",
              cachedIncludePaths != includePaths, cachedDefines != defines);

    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_5");
    targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

    // Add search paths
    eastl::vector<const char*> searchPaths;
    for (const auto& path : includePaths) {
        searchPaths.push_back(path.c_str());
    }
    sessionDesc.searchPaths = searchPaths.data();
    sessionDesc.searchPathCount = searchPaths.size();

    // Add preprocessor defines
    eastl::vector<slang::PreprocessorMacroDesc> macros;
    for (const auto& define : defines) {
        slang::PreprocessorMacroDesc macro = {};
        macro.name = define.c_str();
        macro.value = "1";
        macros.push_back(macro);
    }
    sessionDesc.preprocessorMacros = macros.data();
    sessionDesc.preprocessorMacroCount = macros.size();

    // Create new session
    cachedSession = nullptr;  // Release old session first
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, cachedSession.writeRef()))) {
        Log::error("SlangCompiler", "Failed to create Slang session");
        return nullptr;
    }

    // Cache settings
    cachedIncludePaths = includePaths;
    cachedDefines = defines;

    return cachedSession.get();
}

bool SlangCompiler::checkDiagnostics(slang::IBlob* diagnostics, CompileResult& result) {
    if (diagnostics && diagnostics->getBufferSize() > 0) {
        const char* diagText = reinterpret_cast<const char*>(diagnostics->getBufferPointer());
        result.errorMessage = eastl::string(diagText, diagnostics->getBufferSize());
        Log::error("SlangCompiler", "Compilation diagnostics:\n{}", result.errorMessage.c_str());
        return false;
    }
    return true;
}

} // namespace violet