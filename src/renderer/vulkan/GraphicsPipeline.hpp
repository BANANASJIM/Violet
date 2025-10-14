#pragma once

#include "renderer/vulkan/PipelineBase.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/weak_ptr.h>

namespace violet {

class DescriptorSet;
class Material;
class Shader;

struct PipelineConfig {
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    vk::CullModeFlagBits cullMode = vk::CullModeFlagBits::eBack;
    float lineWidth = 1.0f;
    bool enableDepthTest = true;
    bool enableDepthWrite = true;
    vk::CompareOp depthCompareOp = vk::CompareOp::eLess;  // Default to less for normal depth testing
    bool enableBlending = false;
    bool useVertexInput = true;  // Set to false for full-screen effects like skybox

    // Dynamic rendering formats (replaces RenderPass dependency)
    eastl::vector<vk::Format> colorFormats;
    vk::Format depthFormat = vk::Format::eUndefined;
    vk::Format stencilFormat = vk::Format::eUndefined;

    eastl::vector<vk::PushConstantRange> pushConstantRanges;  // Custom push constant ranges
    eastl::vector<vk::DescriptorSetLayout> additionalDescriptorSets;  // Additional descriptor sets (e.g., bindless)
    vk::DescriptorSetLayout globalDescriptorSetLayout = nullptr;  // Global descriptor set layout from DescriptorManager
    vk::DescriptorSetLayout materialDescriptorSetLayout = nullptr;  // Material descriptor set layout from DescriptorManager
};

class GraphicsPipeline : public PipelineBase {
public:
    GraphicsPipeline() = default;
    ~GraphicsPipeline() override = default;

    /**
     * @brief Initialize pipeline with Shader weak_ptr references (dynamic rendering)
     * @param vertShader Vertex shader weak_ptr (managed by ShaderLibrary)
     * @param fragShader Fragment shader weak_ptr (managed by ShaderLibrary)
     * @param config Pipeline configuration including format information
     */
    void init(VulkanContext* context, Material* material,
              eastl::weak_ptr<Shader> vertShader, eastl::weak_ptr<Shader> fragShader,
              const PipelineConfig& config);

    /**
     * @brief Rebuild pipeline after shader update (hot reload)
     * @return True if rebuild succeeded, false if shaders are no longer valid
     */
    bool rebuild();

    void cleanup() override;

    void bind(vk::CommandBuffer commandBuffer) override;
    vk::PipelineLayout getPipelineLayout() const override { return *pipelineLayout; }
    vk::Pipeline getPipeline() const { return *graphicsPipeline; }

private:
    /**
     * @brief Build Vulkan pipeline from current shader references
     */
    void buildPipeline();

    /**
     * @brief Create ShaderModule from SPIRV bytecode
     */
    vk::raii::ShaderModule createShaderModuleFromSPIRV(const eastl::vector<uint32_t>& spirv);

private:
    // Shader references (weak pointers - owned by ShaderLibrary)
    eastl::weak_ptr<Shader> vertShader;
    eastl::weak_ptr<Shader> fragShader;

    // Cached Vulkan resources
    vk::raii::ShaderModule vertShaderModule{nullptr};
    vk::raii::ShaderModule fragShaderModule{nullptr};
    vk::raii::Pipeline graphicsPipeline{nullptr};

    // Cached configuration for rebuild
    Material* material = nullptr;
    PipelineConfig config;
};

} // namespace violet