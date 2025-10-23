#include "SlangCompiler.hpp"
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
    globalSession = nullptr;
}

ShaderCompiler::CompileResult SlangCompiler::compile(const Shader::CreateInfo& info) {
    CompileResult result;

    if (!globalSession) {
        result.success = false;
        result.errorMessage = "Slang global session not initialized";
        return result;
    }

    // Create session description
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_5");
    targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

    // Add search paths
    eastl::vector<const char*> searchPaths;
    for (const auto& path : info.includePaths) {
        searchPaths.push_back(path.c_str());
    }
    sessionDesc.searchPaths = searchPaths.data();
    sessionDesc.searchPathCount = searchPaths.size();

    // Add preprocessor defines
    eastl::vector<slang::PreprocessorMacroDesc> macros;
    for (const auto& define : info.defines) {
        slang::PreprocessorMacroDesc macro = {};
        macro.name = define.c_str();
        macro.value = "1";
        macros.push_back(macro);
    }
    sessionDesc.preprocessorMacros = macros.data();
    sessionDesc.preprocessorMacroCount = macros.size();

    // Create session
    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
        result.success = false;
        result.errorMessage = "Failed to create Slang session";
        return result;
    }

    // Pre-load common modules that might be imported by shaders
    const char* commonModules[] = {
        "shaders/slang/Common.slang",
        "shaders/slang/PBR.slang",
        "shaders/slang/Sampling.slang",
        "shaders/slang/Utilities.slang"
    };

    for (const char* modulePath : commonModules) {
        Slang::ComPtr<slang::IBlob> moduleDiag;
        slang::IModule* commonModule = session->loadModule(modulePath, moduleDiag.writeRef());
        if (!commonModule) {
            // It's okay if module doesn't exist or fails to load - not all shaders need all modules
            Log::debug("SlangCompiler", "Could not preload module '{}' (this is normal if not needed)", modulePath);
        }
    }

    // Load module
    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = session->loadModule(info.filePath.c_str(), diagnostics.writeRef());

    if (!module) {
        result.success = false;
        checkDiagnostics(diagnostics, result);
        return result;
    }

    // Find entry point
    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    if (SLANG_FAILED(module->findEntryPointByName(info.entryPoint.c_str(), entryPoint.writeRef()))) {
        result.success = false;
        result.errorMessage = "Entry point '" + info.entryPoint + "' not found";
        return result;
    }

    // Create component list
    slang::IComponentType* components[] = {module, entryPoint};
    Slang::ComPtr<slang::IComponentType> program;
    if (SLANG_FAILED(session->createCompositeComponentType(
            components, 2, program.writeRef(), diagnostics.writeRef()))) {
        result.success = false;
        checkDiagnostics(diagnostics, result);
        return result;
    }

    // Link program
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    if (SLANG_FAILED(program->link(linkedProgram.writeRef(), diagnostics.writeRef()))) {
        result.success = false;
        checkDiagnostics(diagnostics, result);
        return result;
    }

    // Get SPIRV code
    Slang::ComPtr<slang::IBlob> spirvCode;
    if (SLANG_FAILED(linkedProgram->getEntryPointCode(
            0, 0, spirvCode.writeRef(), diagnostics.writeRef()))) {
        result.success = false;
        checkDiagnostics(diagnostics, result);
        return result;
    }

    // Copy SPIRV to result
    const uint32_t* spirvData = reinterpret_cast<const uint32_t*>(spirvCode->getBufferPointer());
    size_t spirvSize = spirvCode->getBufferSize() / sizeof(uint32_t);
    result.spirv.resize(spirvSize);
    memcpy(result.spirv.data(), spirvData, spirvCode->getBufferSize());

    // Cache reflection data
    lastLinkedProgram = linkedProgram;
    lastReflection = linkedProgram->getLayout();

    result.success = true;
    result.sourceHash = computeSourceHash(info.filePath);

    Log::debug("SlangCompiler", "Compiled {} bytes of SPIRV from {}, reflection available: {}",
               result.spirv.size() * 4, info.filePath.c_str(), lastReflection != nullptr);

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

    // Create session description
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

    // Create session
    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
        Log::error("SlangCompiler", "Failed to create Slang session for reflection");
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