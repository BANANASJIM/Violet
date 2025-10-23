#pragma once

#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <EASTL/weak_ptr.h>

#include "Shader.hpp"
#include "ShaderCompiler.hpp"

namespace violet {

class VulkanContext;
class DescriptorManager;

/**
 * @brief Central manager for all shader resources
 *
 * Responsibilities:
 * - Load and compile shaders (GLSL/Slang)
 * - Cache compiled SPIRV
 * - Manage shader lifecycle
 * - Support hot reloading
 * - Automatically register descriptor layouts from Slang reflection
 *
 * Usage:
 *   auto shader = shaderLibrary.load("pbr_vertex", {
 *       .filePath = "shaders/pbr.slang",
 *       .entryPoint = "vertexMain",
 *       .stage = Shader::Stage::Vertex,
 *       .language = Shader::Language::Slang
 *   });
 *   pipeline.setShader(shader);
 */
class ShaderLibrary {
public:
    ShaderLibrary() = default;
    explicit ShaderLibrary(VulkanContext* context, DescriptorManager* descriptorManager = nullptr);
    ~ShaderLibrary();

    /**
     * @brief Load Slang shader module with automatic entry point detection
     * @param filePath Path to .slang shader file
     * @return Vector of weak pointers to compiled shaders (one per entry point)
     *
     * Modern API: Automatically detects all entry points (vertex, fragment, compute)
     * via Slang reflection. Shader names are generated as "filename_entrypoint".
     * Descriptor layouts are auto-registered from reflection.
     *
     * Example:
     *   auto shaders = shaderLib.loadSlangShader("shaders/pbr_bindless.slang");
     *   // Returns: ["pbr_bindless_vertexMain", "pbr_bindless_fragmentMain"]
     */
    eastl::vector<eastl::weak_ptr<Shader>> loadSlangShader(const eastl::string& filePath);

    /**
     * @brief Load or retrieve cached shader (DEPRECATED for Slang)
     * @param name Unique shader identifier
     * @param info Shader creation info
     * @return Weak pointer to shader (empty if failed)
     *
     * @deprecated Use loadSlangShader() for Slang shaders with automatic entry detection.
     * This method is kept for backward compatibility with GLSL shaders.
     *
     * Note: Returns weak_ptr to prevent external strong references.
     * ShaderLibrary owns all shaders via shared_ptr internally.
     */
    eastl::weak_ptr<Shader> load(const eastl::string& name, const Shader::CreateInfo& info);

    /**
     * @brief Get shader by name
     * @return Weak pointer to shader (empty if not found)
     */
    eastl::weak_ptr<Shader> get(const eastl::string& name);

    /**
     * @brief Reload shader from source (for hot reload)
     * @return True if reload succeeded
     */
    bool reload(const eastl::string& name);

    /**
     * @brief Reload all shaders from changed sources
     * @return Number of shaders reloaded
     */
    int reloadChanged();

    /**
     * @brief Check if shader exists
     */
    bool has(const eastl::string& name) const;

    /**
     * @brief Remove shader from library
     */
    void remove(const eastl::string& name);

    /**
     * @brief Clear all shaders
     */
    void clear();

    /**
     * @brief Set default shader search paths
     */
    void setIncludePaths(const eastl::vector<eastl::string>& paths);

    /**
     * @brief Add global shader define
     */
    void addGlobalDefine(const eastl::string& define);

    /**
     * @brief Get last error message
     */
    const eastl::string& getLastError() const { return lastError; }

private:
    ShaderCompiler* getCompiler(Shader::Language language);

private:
    VulkanContext* context = nullptr;
    DescriptorManager* descriptorManager = nullptr;

    // Shader storage: name -> Shader (shared_ptr for weak_ptr support + thread safety)
    eastl::unordered_map<eastl::string, eastl::shared_ptr<Shader>> shaders;

    // Compilers for each language
    eastl::unique_ptr<ShaderCompiler> glslCompiler;
    eastl::unique_ptr<ShaderCompiler> slangCompiler;

    // Default compilation options
    eastl::vector<eastl::string> defaultIncludePaths;
    eastl::vector<eastl::string> globalDefines;

    eastl::string lastError;
};

} // namespace violet