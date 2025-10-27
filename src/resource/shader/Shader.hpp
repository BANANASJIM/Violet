#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <slang.h>  // For ProgramLayout

namespace violet {

// Forward declarations
class DescriptorManager;
struct DescriptorLayoutDesc;
struct PushConstantInfo;
using LayoutHandle = uint32_t;
using PushConstantHandle = uint32_t;

/**
 * @brief Shader resource encapsulating SPIRV bytecode and metadata
 *
 * Shader objects are managed by ShaderLibrary and referenced by Pipelines.
 * Supports hot reloading by recompiling and updating SPIRV data.
 */
class Shader {
public:
    enum class Stage {
        Vertex,
        Fragment,
        Compute,
        Geometry,
        TessControl,
        TessEvaluation
    };

    enum class Language {
        GLSL,
        Slang
    };

    struct CreateInfo {
        eastl::string name;           // Shader identifier (e.g., "pbr_vertex")
        eastl::string filePath;       // Source file path
        eastl::string entryPoint;     // Entry point function name
        Stage stage;                  // Shader stage
        Language language;            // Source language
        eastl::vector<eastl::string> includePaths;
        eastl::vector<eastl::string> defines;
    };

    Shader(const CreateInfo& info, const eastl::vector<uint32_t>& spirv);
    ~Shader();

    // Accessors
    const eastl::string& getName() const { return name; }
    const eastl::string& getFilePath() const { return filePath; }
    const eastl::string& getEntryPoint() const { return entryPoint; }
    Stage getStage() const { return stage; }
    Language getLanguage() const { return language; }
    const eastl::vector<uint32_t>& getSPIRV() const { return spirvCode; }
    size_t getSourceHash() const { return sourceHash; }

    /**
     * @brief Update SPIRV code (for hot reload)
     */
    void updateSPIRV(const eastl::vector<uint32_t>& spirv, size_t newHash);

    /**
     * @brief Convert Stage enum to Vulkan shader stage flag
     */
    static vk::ShaderStageFlagBits stageToVkFlag(Stage stage);

    /**
     * @brief Convert Stage enum to string
     */
    static const char* stageToString(Stage stage);

    // Reflection API (Slang only)
    void setReflection(slang::ProgramLayout* layout);
    bool hasReflection() const { return reflection != nullptr; }
    slang::ProgramLayout* getReflection() const { return reflection; }

    // Get extracted shader reflection (all resources)
    const class ShaderReflection* getShaderReflection() const { return shaderReflection; }

    void registerDescriptorLayouts(DescriptorManager* manager);
    const eastl::vector<LayoutHandle>& getDescriptorLayoutHandles() const { return descriptorLayoutHandles; }
    PushConstantHandle getPushConstantHandle() const { return pushConstantHandle; }

private:
    eastl::string name;
    eastl::string filePath;
    eastl::string entryPoint;
    Stage stage;
    Language language;
    eastl::vector<uint32_t> spirvCode;
    size_t sourceHash;

    // Compilation options (for recompilation)
    eastl::vector<eastl::string> includePaths;
    eastl::vector<eastl::string> defines;

    // Reflection data (Slang only)
    slang::ProgramLayout* reflection = nullptr;
    class ShaderReflection* shaderReflection = nullptr;  // Extracted resource info
    eastl::vector<LayoutHandle> descriptorLayoutHandles;  // Ordered by set index
    PushConstantHandle pushConstantHandle = 0;  // 0 = no push constants
};

} // namespace violet