#pragma once

#include "renderer/vulkan/PipelineBase.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/weak_ptr.h>

namespace violet {

class DescriptorSet;
class DescriptorManager;
class Material;
class Shader;

// Declarative pipeline configuration with sensible defaults
struct PipelineConfig {
    // Topology
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    bool primitiveRestartEnable = false;

    // Vertex input
    bool useVertexInput = true;  // false for fullscreen passes

    // Rasterization
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    vk::CullModeFlagBits cullMode = vk::CullModeFlagBits::eBack;
    vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
    float lineWidth = 1.0f;
    bool depthClampEnable = false;
    bool rasterizerDiscardEnable = false;
    bool depthBiasEnable = false;

    // Multisampling
    vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
    bool sampleShadingEnable = false;

    // Depth/Stencil
    bool enableDepthTest = true;
    bool enableDepthWrite = true;
    vk::CompareOp depthCompareOp = vk::CompareOp::eLess;
    bool depthBoundsTestEnable = false;
    bool stencilTestEnable = false;

    // Color blending
    bool enableBlending = false;
    vk::BlendFactor srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    vk::BlendFactor dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    vk::BlendOp colorBlendOp = vk::BlendOp::eAdd;
    vk::BlendFactor srcAlphaBlendFactor = vk::BlendFactor::eOne;
    vk::BlendFactor dstAlphaBlendFactor = vk::BlendFactor::eZero;
    vk::BlendOp alphaBlendOp = vk::BlendOp::eAdd;

    // Dynamic rendering formats
    eastl::vector<vk::Format> colorFormats;
    vk::Format depthFormat = vk::Format::eUndefined;
    vk::Format stencilFormat = vk::Format::eUndefined;

    // Dynamic states (viewport and scissor are always dynamic)
    eastl::vector<vk::DynamicState> additionalDynamicStates;

    // @deprecated Push constants now auto-extracted from shader reflection
    eastl::vector<vk::PushConstantRange> pushConstantRanges;

    // @deprecated Descriptor layouts now auto-extracted from shader reflection
    eastl::vector<vk::DescriptorSetLayout> additionalDescriptorSets;
    // @deprecated Use shader reflection instead
    vk::DescriptorSetLayout globalDescriptorSetLayout = nullptr;
    // @deprecated Use shader reflection instead
    vk::DescriptorSetLayout materialDescriptorSetLayout = nullptr;
};

class GraphicsPipeline : public PipelineBase {
public:
    GraphicsPipeline() = default;
    ~GraphicsPipeline() override = default;

    // Initialize pipeline - layouts and push constants auto-extracted from shader reflection
    void init(VulkanContext* context,
              DescriptorManager* descriptorManager,
              Material* material,
              eastl::weak_ptr<Shader> vertShader,
              eastl::weak_ptr<Shader> fragShader,
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

    /**
     * @brief Merged shader resources (layouts + push constants)
     */
    struct MergedShaderResources {
        eastl::vector<vk::DescriptorSetLayout> setLayouts;  // Ordered by set index, may have nullptrs for empty sets
        eastl::vector<vk::PushConstantRange> pushConstants;
    };

    /**
     * @brief Merge descriptor layouts and push constants from vertex and fragment shaders
     * Preserves set index sparsity and uses cached handles from shaders
     */
    MergedShaderResources mergeShaderResources(eastl::shared_ptr<Shader> vert, eastl::shared_ptr<Shader> frag);

private:
    // Shader references (weak pointers - owned by ShaderLibrary)
    eastl::weak_ptr<Shader> vertShader;
    eastl::weak_ptr<Shader> fragShader;

    // Cached Vulkan resources
    vk::raii::ShaderModule vertShaderModule{nullptr};
    vk::raii::ShaderModule fragShaderModule{nullptr};
    vk::raii::Pipeline graphicsPipeline{nullptr};

    // Cached configuration for rebuild
    DescriptorManager* descriptorManager = nullptr;
    Material* material = nullptr;
    PipelineConfig config;
};

} // namespace violet