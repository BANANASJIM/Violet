#pragma once

#include "renderer/vulkan/PipelineBase.hpp"
#include "resource/shader/ShaderReflection.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/weak_ptr.h>
#include <EASTL/unique_ptr.h>

namespace violet {

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
};

class GraphicsPipeline : public PipelineBase {
public:
    GraphicsPipeline() = default;
    ~GraphicsPipeline() override = default;

    void init(VulkanContext* context,
              DescriptorManager* descriptorManager,
              Material* material,
              const eastl::vector<eastl::weak_ptr<Shader>>& shaders,
              const PipelineConfig& config);

    bool rebuild();
    void cleanup() override;
    void bind(vk::CommandBuffer commandBuffer) override;

    vk::PipelineLayout getPipelineLayout() const override { return *pipelineLayout; }
    vk::PipelineBindPoint getBindPoint() const override { return vk::PipelineBindPoint::eGraphics; }
    vk::Pipeline getPipeline() const { return *graphicsPipeline; }

private:
    void buildPipeline();
    vk::raii::ShaderModule createShaderModuleFromSPIRV(const eastl::vector<uint32_t>& spirv);

    struct MergedShaderResources {
        eastl::vector<vk::DescriptorSetLayout> setLayouts;
        eastl::vector<vk::PushConstantRange> pushConstants;
    };

    MergedShaderResources mergeShaderResources(const eastl::vector<eastl::shared_ptr<Shader>>& shaderPtrs);
    void mergeReflection(const eastl::vector<eastl::shared_ptr<Shader>>& shaderPtrs);
    void registerDescriptorLayouts();

private:
    eastl::vector<eastl::weak_ptr<Shader>> shaders;
    eastl::vector<vk::raii::ShaderModule> shaderModules;
    vk::raii::Pipeline graphicsPipeline{nullptr};

    DescriptorManager* descriptorManager = nullptr;
    Material* material = nullptr;
    PipelineConfig config;
};

} // namespace violet