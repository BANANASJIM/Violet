#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
class RenderPass;
class DescriptorSet;
class Material;

class Pipeline {
public:
    void init(VulkanContext* context, RenderPass* renderPass, DescriptorSet* globalDescriptorSet, Material* material, const eastl::string& vertPath, const eastl::string& fragPath);
    void cleanup();

    vk::Pipeline getPipeline() const { return graphicsPipeline; }
    vk::PipelineLayout getLayout() const { return pipelineLayout; }

    void bind(vk::CommandBuffer commandBuffer);

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