#include "renderer/core/ComputePass.hpp"
#include "renderer/core/VulkanContext.hpp"
#include "core/Log.hpp"
#include "core/FileSystem.hpp"

namespace violet {

ComputePass::ComputePass() = default;

ComputePass::~ComputePass() {
    cleanup();
}

void ComputePass::init(VulkanContext* ctx, const ComputePassConfig& cfg) {
    context = ctx;
    config = cfg;

    // Create compute pipeline
    pipeline = eastl::make_unique<ComputePipeline>();

    ComputePipelineConfig pipelineConfig;
    pipelineConfig.descriptorSetLayouts = config.descriptorSetLayouts;
    pipelineConfig.pushConstantRanges = config.pushConstantRanges;

    pipeline->init(context, config.shaderPath, pipelineConfig);

    violet::Log::info("Renderer", "ComputePass '{}' initialized with shader: {}",
                     config.name.c_str(), config.shaderPath.c_str());
}

void ComputePass::cleanup() {
    if (pipeline) {
        pipeline->cleanup();
        pipeline.reset();
    }
}

void ComputePass::begin(vk::CommandBuffer cmd, uint32_t frameIndex) {
    // Insert pre-execution barriers
    insertPreBarriers(cmd);

    // Bind compute pipeline
    if (pipeline) {
        pipeline->bind(cmd);
    }
}

void ComputePass::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    // Execute user-defined compute work
    if (config.execute) {
        config.execute(cmd, frameIndex);
    }
}

void ComputePass::end(vk::CommandBuffer cmd) {
    // Insert post-execution barriers
    insertPostBarriers(cmd);
}

void ComputePass::insertPreBarriers(vk::CommandBuffer cmd) {
    if (config.preBarriers.empty()) {
        return;
    }

    eastl::vector<vk::ImageMemoryBarrier> imageBarriers;
    imageBarriers.reserve(config.preBarriers.size());

    for (const auto& barrierConfig : config.preBarriers) {
        vk::ImageMemoryBarrier barrier;
        barrier.srcAccessMask = barrierConfig.srcAccess;
        barrier.dstAccessMask = barrierConfig.dstAccess;
        barrier.oldLayout = barrierConfig.oldLayout;
        barrier.newLayout = barrierConfig.newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = barrierConfig.image;
        barrier.subresourceRange = barrierConfig.subresourceRange;

        imageBarriers.push_back(barrier);
    }

    // Use the first barrier's stage configuration (assuming all barriers have same stages)
    vk::PipelineStageFlags srcStage = config.preBarriers[0].srcStage;
    vk::PipelineStageFlags dstStage = config.preBarriers[0].dstStage;

    cmd.pipelineBarrier(
        srcStage,
        dstStage,
        {},
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data()
    );
}

void ComputePass::insertPostBarriers(vk::CommandBuffer cmd) {
    if (config.postBarriers.empty()) {
        return;
    }

    eastl::vector<vk::ImageMemoryBarrier> imageBarriers;
    imageBarriers.reserve(config.postBarriers.size());

    for (const auto& barrierConfig : config.postBarriers) {
        vk::ImageMemoryBarrier barrier;
        barrier.srcAccessMask = barrierConfig.srcAccess;
        barrier.dstAccessMask = barrierConfig.dstAccess;
        barrier.oldLayout = barrierConfig.oldLayout;
        barrier.newLayout = barrierConfig.newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = barrierConfig.image;
        barrier.subresourceRange = barrierConfig.subresourceRange;

        imageBarriers.push_back(barrier);
    }

    // Use the first barrier's stage configuration (assuming all barriers have same stages)
    vk::PipelineStageFlags srcStage = config.postBarriers[0].srcStage;
    vk::PipelineStageFlags dstStage = config.postBarriers[0].dstStage;

    cmd.pipelineBarrier(
        srcStage,
        dstStage,
        {},
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data()
    );
}

} // namespace violet
