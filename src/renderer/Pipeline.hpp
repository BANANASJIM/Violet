#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
class RenderPass;
class DescriptorSet;
class Material;

struct PipelineConfig {
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    vk::CullModeFlagBits cullMode = vk::CullModeFlagBits::eBack;
    float lineWidth = 1.0f;
    bool enableDepthTest = true;
    bool enableDepthWrite = true;
    bool enableBlending = false;
};

class Pipeline {
public:
    void init(VulkanContext* context, RenderPass* renderPass, DescriptorSet* globalDescriptorSet, Material* material, const eastl::string& vertPath, const eastl::string& fragPath);
    void init(VulkanContext* context, RenderPass* renderPass, DescriptorSet* globalDescriptorSet, Material* material, const eastl::string& vertPath, const eastl::string& fragPath, const PipelineConfig& config);
    void cleanup();

    vk::Pipeline getPipeline() const { return *graphicsPipeline; }
    vk::PipelineLayout getPipelineLayout() const { return *pipelineLayout; }

    void bind(vk::CommandBuffer commandBuffer);

private:
    eastl::vector<char> readFile(const eastl::string& filename);
    vk::raii::ShaderModule createShaderModule(const eastl::vector<char>& code);

private:
    VulkanContext* context = nullptr;
    vk::raii::ShaderModule vertShaderModule{nullptr};
    vk::raii::ShaderModule fragShaderModule{nullptr};
    vk::raii::PipelineLayout pipelineLayout{nullptr};
    vk::raii::Pipeline graphicsPipeline{nullptr};
};

}