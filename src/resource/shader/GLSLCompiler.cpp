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
    result.shader = nullptr;
    result.errorMessage = "GLSL support is deprecated. Please use Slang shaders (.slang files) instead.";

    Log::error("GLSLCompiler", "GLSL compilation attempted but GLSL is deprecated. Use Slang shaders instead.");

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
    // GLSL is deprecated - this function is no longer used
    result.shader = nullptr;
    result.errorMessage = "GLSL support is deprecated. Cannot load pre-compiled SPIRV.";
    return false;
}

} // namespace violet