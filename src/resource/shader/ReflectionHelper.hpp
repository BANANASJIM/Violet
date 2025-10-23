#pragma once

#include <slang.h>
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>

// Forward declare DescriptorManager types to avoid circular dependency
namespace violet {
    enum class UpdateFrequency;
    struct DescriptorLayoutDesc;
    struct BindingDesc;
}

namespace violet {

class ReflectionHelper {
public:
    struct PushConstantInfo {
        uint32_t offset = 0;
        uint32_t size = 0;
        vk::ShaderStageFlags stageFlags = {};
    };

    explicit ReflectionHelper(slang::ProgramLayout* layout, SlangSession* session = nullptr);

    // NEW: Returns DescriptorManager-compatible layout descriptions
    eastl::vector<DescriptorLayoutDesc> extractDescriptorLayouts(
        const eastl::string& shaderName
    ) const;

    eastl::vector<PushConstantInfo> extractPushConstants() const;
    vk::ShaderStageFlags getShaderStageFlags() const;

    bool isValid() const { return layout != nullptr; }

private:
    static vk::DescriptorType slangTypeToVulkan(slang::BindingType type);
    static vk::ShaderStageFlags slangStageToVulkan(SlangStage stage);

    // Infer update frequency from binding types
    static UpdateFrequency inferUpdateFrequency(const eastl::vector<BindingDesc>& bindings);

    slang::ProgramLayout* layout;
    SlangSession* session;  // For querying user attributes
};

} // namespace violet