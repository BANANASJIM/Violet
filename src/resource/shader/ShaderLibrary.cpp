#include "ShaderLibrary.hpp"
#include "GLSLCompiler.hpp"
#include "SlangCompiler.hpp"
#include "ShaderReflection.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"

namespace violet {

ShaderLibrary::ShaderLibrary(VulkanContext* ctx)
    : context(ctx)
    , glslCompiler(eastl::make_unique<GLSLCompiler>())
    , slangCompiler(eastl::make_unique<SlangCompiler>()) {

    // Set default include paths (use absolute paths for Slang module resolution)
    defaultIncludePaths.push_back(FileSystem::resolveRelativePath("shaders"));
    defaultIncludePaths.push_back(FileSystem::resolveRelativePath("shaders/slang"));
    defaultIncludePaths.push_back(FileSystem::resolveRelativePath("shaders/include"));

    Log::info("ShaderLibrary", "Initialized with GLSL and Slang compilers");
}

ShaderLibrary::~ShaderLibrary() {
    clear();
}

eastl::vector<eastl::weak_ptr<Shader>> ShaderLibrary::loadSlangShader(const eastl::string& filePath) {
    eastl::vector<eastl::weak_ptr<Shader>> loadedShaders;

    if (!slangCompiler) {
        Log::error("ShaderLibrary", "Slang compiler not initialized");
        return loadedShaders;
    }

    // Get all entry points from the module via reflection
    auto* slang = static_cast<SlangCompiler*>(slangCompiler.get());
    auto entryPoints = slang->getModuleEntryPoints(filePath, defaultIncludePaths);

    if (entryPoints.empty()) {
        Log::warn("ShaderLibrary", "No entry points found in module '{}'", filePath.c_str());
        return loadedShaders;
    }

    eastl::string baseName = filePath;
    size_t lastSlash = baseName.find_last_of("/\\");
    if (lastSlash != eastl::string::npos) {
        baseName = baseName.substr(lastSlash + 1);
    }
    size_t lastDot = baseName.find_last_of('.');
    if (lastDot != eastl::string::npos) {
        baseName = baseName.substr(0, lastDot);
    }

    // Compile each entry point as a separate shader
    for (const auto& entryPoint : entryPoints) {
        // Generate shader name using simplified stage names
        // Stage name mapping: Vertex->vertex, Fragment->frag, Compute->comp, etc.
        const char* stageNames[] = {"vertex", "frag", "comp", "geom", "tesc", "tese"};
        const char* stageName = stageNames[static_cast<int>(entryPoint.stage)];

        eastl::string shaderName = baseName + "_" + stageName;
        // Result: "pbr_bindless_vertex", "pbr_bindless_frag"

        // Check if already loaded
        if (has(shaderName)) {
            Log::debug("ShaderLibrary", "Shader '{}' already loaded, skipping", shaderName.c_str());
            loadedShaders.push_back(get(shaderName));
            continue;
        }

        // Create shader info
        Shader::CreateInfo info;
        info.name = shaderName;
        info.filePath = filePath;
        info.entryPoint = entryPoint.name;
        info.stage = entryPoint.stage;
        info.language = Shader::Language::Slang;
        info.includePaths = defaultIncludePaths;
        info.defines = globalDefines;

        // Load (compile + auto-register layouts)
        auto shader = load(shaderName, info);
        loadedShaders.push_back(shader);

        if (!shader.expired()) {
            Log::info("ShaderLibrary", "  ✓ Loaded '{}' ({})",
                     shaderName.c_str(), stageName);
        } else {
            Log::error("ShaderLibrary", "  ✗ Failed to load '{}' ({})",
                      shaderName.c_str(), entryPoint.name.c_str());
        }
    }

    Log::info("ShaderLibrary", "Loaded {} shaders from '{}'",
             loadedShaders.size(), filePath.c_str());

    return loadedShaders;
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

    // Compile shader - returns complete Shader object with reflection
    Log::info("ShaderLibrary", "Compiling Slang shader '{}' from {}", name.c_str(), info.filePath.c_str());
    auto result = compiler->compile(mergedInfo);

    if (!result.shader) {
        lastError = result.errorMessage;
        Log::error("ShaderLibrary", "Failed to compile shader '{}': {}",
                   name.c_str(), result.errorMessage.c_str());
        return eastl::weak_ptr<Shader>();
    }

    // Note: Descriptor layouts are registered by Pipeline during creation,
    // not here. This keeps ShaderLibrary pure resource management.

    shaders[name] = result.shader;

    Log::info("ShaderLibrary", "Successfully loaded shader '{}'", name.c_str());

    return eastl::weak_ptr<Shader>(result.shader);
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

    if (!result.shader) {
        lastError = result.errorMessage;
        Log::error("ShaderLibrary", "Failed to reload shader '{}': {}",
                   name.c_str(), result.errorMessage.c_str());
        return false;
    }

    // Replace old shader with new one
    shaders[name] = result.shader;

    // Note: Pipelines using this shader must be rebuilt after reload
    // to re-register descriptor layouts. Hot reload requires pipeline recreation.

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