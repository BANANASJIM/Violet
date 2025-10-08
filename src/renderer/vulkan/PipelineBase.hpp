#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;

class PipelineBase {
public:
    PipelineBase() = default;
    virtual ~PipelineBase() = default;

    PipelineBase(const PipelineBase&) = delete;
    PipelineBase& operator=(const PipelineBase&) = delete;

    PipelineBase(PipelineBase&&) = default;
    PipelineBase& operator=(PipelineBase&&) = default;

    virtual void bind(vk::CommandBuffer commandBuffer) = 0;
    virtual vk::PipelineLayout getPipelineLayout() const = 0;
    virtual void cleanup();

protected:
    eastl::vector<char> readFile(const eastl::string& filename);
    vk::raii::ShaderModule createShaderModule(const eastl::vector<char>& code);

protected:
    VulkanContext* context = nullptr;
    vk::raii::PipelineLayout pipelineLayout{nullptr};
};

} // namespace violet