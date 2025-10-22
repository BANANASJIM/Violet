#pragma once

#include <slang.h>
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace violet {

class ReflectionHelper {
public:
    struct BindingInfo {
        uint32_t set = 0;
        uint32_t binding = 0;
        vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;
        uint32_t descriptorCount = 1;
        vk::ShaderStageFlags stageFlags = {};
        eastl::string name;
        bool isBindless = false;
    };

    struct SetLayoutInfo {
        uint32_t setIndex = 0;
        eastl::vector<BindingInfo> bindings;
        vk::DescriptorSetLayoutCreateFlags flags = {};
    };

    struct PushConstantInfo {
        uint32_t offset = 0;
        uint32_t size = 0;
        vk::ShaderStageFlags stageFlags = {};
    };

    explicit ReflectionHelper(slang::ProgramLayout* layout);

    eastl::vector<SetLayoutInfo> extractDescriptorSetLayouts() const;
    eastl::vector<PushConstantInfo> extractPushConstants() const;
    vk::ShaderStageFlags getShaderStageFlags() const;

    bool isValid() const { return layout != nullptr; }

private:
    static vk::DescriptorType slangTypeToVulkan(slang::BindingType type);
    static vk::ShaderStageFlags slangStageToVulkan(SlangStage stage);

    void processParameter(
        slang::VariableLayoutReflection* param,
        uint32_t setIndex,
        eastl::vector<BindingInfo>& bindings
    ) const;

    slang::ProgramLayout* layout;
};

} // namespace violet