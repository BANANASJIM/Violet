#include "GLSLCompiler.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace violet {

GLSLCompiler::GLSLCompiler() {
    glslcPath = findGlslc();
    if (!glslcPath.empty()) {
        Log::info("GLSLCompiler", "Found glslc at: {}", glslcPath.c_str());
    } else {
        Log::warn("GLSLCompiler", "glslc not found, runtime compilation unavailable");
    }
}

ShaderCompiler::CompileResult GLSLCompiler::compile(const Shader::CreateInfo& info) {
    CompileResult result;

    // Try to load pre-compiled SPIRV first (from CMake build)
    if (loadPrecompiledSPIRV(info.filePath, result)) {
        result.sourceHash = computeSourceHash(info.filePath);
        return result;
    }

    // Runtime compilation not yet implemented
    // TODO: Implement runtime glslc invocation for hot reload
    result.success = false;
    result.errorMessage = "Runtime GLSL compilation not yet implemented. Use pre-compiled shaders.";
    Log::warn("GLSLCompiler", "Runtime compilation requested but not yet implemented");

    return result;
}

bool GLSLCompiler::hasSourceChanged(const eastl::string& filePath, size_t lastHash) const {
    size_t currentHash = computeSourceHash(filePath);
    return currentHash != lastHash && currentHash != 0;
}

size_t GLSLCompiler::computeSourceHash(const eastl::string& filePath) const {
    struct stat fileInfo;
    if (stat(filePath.c_str(), &fileInfo) != 0) {
        return 0;
    }

    // Simple hash: combine file size and modification time
    size_t hash = fileInfo.st_size;
    hash ^= fileInfo.st_mtime + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    return hash;
}

const char* GLSLCompiler::stageToExtension(Shader::Stage stage) {
    switch (stage) {
        case Shader::Stage::Vertex:         return ".vert";
        case Shader::Stage::Fragment:       return ".frag";
        case Shader::Stage::Compute:        return ".comp";
        case Shader::Stage::Geometry:       return ".geom";
        case Shader::Stage::TessControl:    return ".tesc";
        case Shader::Stage::TessEvaluation: return ".tese";
    }
    return "";
}

eastl::string GLSLCompiler::findGlslc() const {
    // Try common locations
    const char* paths[] = {
        "glslc",
        "/usr/bin/glslc",
        "/usr/local/bin/glslc",
    };

    for (const char* path : paths) {
        FILE* pipe = popen((eastl::string(path) + " --version 2>/dev/null").c_str(), "r");
        if (pipe) {
            pclose(pipe);
            return path;
        }
    }

    return "";
}

bool GLSLCompiler::loadPrecompiledSPIRV(const eastl::string& filePath, CompileResult& result) {
    // Convert source path to SPIRV path
    // filePath is the GLSL source file path (e.g., "/absolute/path/to/shaders/pbr.vert")
    // We need to find the pre-compiled .spv in build/shaders/

    // Extract just the filename from the full path
    size_t lastSlash = filePath.find_last_of('/');
    eastl::string filename = (lastSlash != eastl::string::npos) ?
                             filePath.substr(lastSlash + 1) : filePath;

    // Construct build path: build/shaders/filename.spv
    // CRITICAL: Use FileSystem::resolveRelativePath() to handle different working directories
    eastl::string buildPath = FileSystem::resolveRelativePath(
        eastl::string("build/shaders/") + filename + ".spv"
    );
    auto spirvData = FileSystem::readBinary(buildPath);

    if (spirvData.empty()) {
        // Try direct path with .spv extension as fallback
        spirvData = FileSystem::readBinary(filePath + ".spv");
    }

    if (spirvData.empty()) {
        result.success = false;
        result.errorMessage = "Pre-compiled SPIRV not found at: " + buildPath;
        return false;
    }

    // Convert bytes to uint32_t
    if (spirvData.size() % 4 != 0) {
        result.success = false;
        result.errorMessage = "Invalid SPIRV file size (not multiple of 4 bytes)";
        return false;
    }

    result.spirv.resize(spirvData.size() / 4);
    memcpy(result.spirv.data(), spirvData.data(), spirvData.size());

    result.success = true;
    Log::debug("GLSLCompiler", "Loaded pre-compiled SPIRV from: {}", buildPath.c_str());

    return true;
}

} // namespace violet