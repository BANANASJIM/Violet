#pragma once

#include "renderer/vulkan/PipelineBase.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/weak_ptr.h>

namespace violet {

class VulkanContext;
class Shader;

struct ComputePipelineConfig {
    eastl::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
    eastl::vector<vk::PushConstantRange> pushConstantRanges;
};

class ComputePipeline : public PipelineBase {
public:
    ComputePipeline() = default;
    ~ComputePipeline() override = default;

    /**
     * @brief Initialize pipeline with Shader weak_ptr reference
     * @param shader Compute shader weak_ptr (managed by ShaderLibrary)
     */
    void init(VulkanContext* context, eastl::weak_ptr<Shader> shader,
              const ComputePipelineConfig& config = {});

    /**
     * @brief Rebuild pipeline after shader update (hot reload)
     * @return True if rebuild succeeded, false if shader is no longer valid
     */
    bool rebuild();

    void cleanup() override;

    void bind(vk::CommandBuffer commandBuffer) override;
    vk::PipelineLayout getPipelineLayout() const override { return *pipelineLayout; }
    vk::Pipeline getPipeline() const { return *computePipeline; }

    void dispatch(vk::CommandBuffer commandBuffer, uint32_t groupCountX,
                  uint32_t groupCountY, uint32_t groupCountZ);

private:
    /**
     * @brief Build Vulkan pipeline from current shader reference
     */
    void buildPipeline();

    /**
     * @brief Create ShaderModule from SPIRV bytecode
     */
    vk::raii::ShaderModule createShaderModuleFromSPIRV(const eastl::vector<uint32_t>& spirv);

private:
    // Shader reference (weak pointer - owned by ShaderLibrary)
    eastl::weak_ptr<Shader> computeShader;

    // Cached Vulkan resources
    vk::raii::ShaderModule computeShaderModule{nullptr};
    vk::raii::Pipeline computePipeline{nullptr};

    // Cached configuration for rebuild
    ComputePipelineConfig config;
};

} // namespace violet