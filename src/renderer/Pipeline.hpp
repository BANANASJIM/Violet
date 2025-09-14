#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
class RenderPass;

class DescriptorSet;

class Pipeline {
public:
    void init(VulkanContext* context, RenderPass* renderPass, DescriptorSet* descriptorSet, const eastl::string& vertPath, const eastl::string& fragPath);
    void cleanup();

    vk::Pipeline getPipeline() const { return graphicsPipeline; }
    vk::PipelineLayout getLayout() const { return pipelineLayout; }

private:
    eastl::vector<char> readFile(const eastl::string& filename);
    vk::ShaderModule createShaderModule(const eastl::vector<char>& code);

private:
    VulkanContext* context = nullptr;
    vk::ShaderModule vertShaderModule;
    vk::ShaderModule fragShaderModule;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline graphicsPipeline;
};

}