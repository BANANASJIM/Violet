#include "ShaderLibrary.hpp"
#include "GLSLCompiler.hpp"
#include "SlangCompiler.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"

namespace violet {

ShaderLibrary::ShaderLibrary(VulkanContext* ctx)
    : context(ctx)
    , glslCompiler(eastl::make_unique<GLSLCompiler>())
    , slangCompiler(eastl::make_unique<SlangCompiler>()) {

    // Set default include paths
    defaultIncludePaths.push_back("shaders");
    defaultIncludePaths.push_back("shaders/include");

    Log::info("ShaderLibrary", "Initialized with GLSL and Slang compilers");
}

ShaderLibrary::~ShaderLibrary() {
    clear();
}

eastl::weak_ptr<Shader> ShaderLibrary::load(const eastl::string& name, const Shader::CreateInfo& info) {
    // Check if already loaded
    auto it = shaders.find(name);
    if (it != shaders.end()) {
        Log::debug("ShaderLibrary", "Shader '{}' already loaded, returning cached version", name.c_str());
        return eastl::weak_ptr<Shader>(it->second);
    }

    // Merge default options with provided options
    Shader::CreateInfo mergedInfo = info;
    mergedInfo.name = name;

    // Add default include paths
    for (const auto& path : defaultIncludePaths) {
        if (eastl::find(mergedInfo.includePaths.begin(), mergedInfo.includePaths.end(), path)
            == mergedInfo.includePaths.end()) {
            mergedInfo.includePaths.push_back(path);
        }
    }

    // Add global defines
    for (const auto& define : globalDefines) {
        if (eastl::find(mergedInfo.defines.begin(), mergedInfo.defines.end(), define)
            == mergedInfo.defines.end()) {
            mergedInfo.defines.push_back(define);
        }
    }

    // Get appropriate compiler
    ShaderCompiler* compiler = getCompiler(info.language);
    if (!compiler) {
        lastError = "No compiler available for language";
        Log::error("ShaderLibrary", "Failed to get compiler for shader '{}'", name.c_str());
        return eastl::weak_ptr<Shader>();
    }

    // Compile shader
    Log::info("ShaderLibrary", "Compiling shader '{}' from {}", name.c_str(), info.filePath.c_str());
    auto result = compiler->compile(mergedInfo);

    if (!result.success) {
        lastError = result.errorMessage;
        Log::error("ShaderLibrary", "Failed to compile shader '{}': {}",
                   name.c_str(), result.errorMessage.c_str());
        return eastl::weak_ptr<Shader>();
    }

    // Create shader object with shared_ptr
    auto shader = eastl::make_shared<Shader>(mergedInfo, result.spirv);
    shader->updateSPIRV(result.spirv, result.sourceHash);

    shaders[name] = shader;

    Log::info("ShaderLibrary", "Successfully loaded shader '{}' ({} bytes SPIRV)",
              name.c_str(), result.spirv.size() * 4);

    return eastl::weak_ptr<Shader>(shader);
}

eastl::weak_ptr<Shader> ShaderLibrary::get(const eastl::string& name) {
    auto it = shaders.find(name);
    if (it != shaders.end()) {
        return eastl::weak_ptr<Shader>(it->second);
    }
    return eastl::weak_ptr<Shader>();
}

bool ShaderLibrary::reload(const eastl::string& name) {
    auto it = shaders.find(name);
    if (it == shaders.end()) {
        lastError = "Shader not found";
        return false;
    }

    Shader* shader = it->second.get();
    ShaderCompiler* compiler = getCompiler(shader->getLanguage());

    if (!compiler) {
        lastError = "No compiler available";
        return false;
    }

    // Check if source has changed
    size_t currentHash = compiler->computeSourceHash(shader->getFilePath());
    if (currentHash == shader->getSourceHash()) {
        Log::debug("ShaderLibrary", "Shader '{}' source unchanged, skipping reload", name.c_str());
        return true;
    }

    // Recompile
    Shader::CreateInfo info;
    info.name = shader->getName();
    info.filePath = shader->getFilePath();
    info.entryPoint = shader->getEntryPoint();
    info.stage = shader->getStage();
    info.language = shader->getLanguage();

    Log::info("ShaderLibrary", "Reloading shader '{}'...", name.c_str());
    auto result = compiler->compile(info);

    if (!result.success) {
        lastError = result.errorMessage;
        Log::error("ShaderLibrary", "Failed to reload shader '{}': {}",
                   name.c_str(), result.errorMessage.c_str());
        return false;
    }

    // Update SPIRV
    shader->updateSPIRV(result.spirv, result.sourceHash);

    Log::info("ShaderLibrary", "Successfully reloaded shader '{}'", name.c_str());
    return true;
}

int ShaderLibrary::reloadChanged() {
    int reloadCount = 0;

    for (auto& [name, shader] : shaders) {
        ShaderCompiler* compiler = getCompiler(shader->getLanguage());
        if (!compiler) continue;

        if (compiler->hasSourceChanged(shader->getFilePath(), shader->getSourceHash())) {
            if (reload(name)) {
                reloadCount++;
            }
        }
    }

    if (reloadCount > 0) {
        Log::info("ShaderLibrary", "Reloaded {} shader(s)", reloadCount);
    }

    return reloadCount;
}

bool ShaderLibrary::has(const eastl::string& name) const {
    return shaders.find(name) != shaders.end();
}

void ShaderLibrary::remove(const eastl::string& name) {
    auto it = shaders.find(name);
    if (it != shaders.end()) {
        Log::debug("ShaderLibrary", "Removing shader '{}'", name.c_str());
        shaders.erase(it);
    }
}

void ShaderLibrary::clear() {
    Log::info("ShaderLibrary", "Clearing all shaders ({} total)", shaders.size());
    shaders.clear();
}

void ShaderLibrary::setIncludePaths(const eastl::vector<eastl::string>& paths) {
    defaultIncludePaths = paths;
}

void ShaderLibrary::addGlobalDefine(const eastl::string& define) {
    globalDefines.push_back(define);
}

ShaderCompiler* ShaderLibrary::getCompiler(Shader::Language language) {
    switch (language) {
        case Shader::Language::GLSL:  return glslCompiler.get();
        case Shader::Language::Slang: return slangCompiler.get();
    }
    return nullptr;
}

} // namespace violet