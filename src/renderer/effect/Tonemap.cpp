#include "Tonemap.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "resource/MaterialManager.hpp"
#include "resource/Material.hpp"
#include "resource/shader/ShaderLibrary.hpp"
#include "renderer/graph/RenderGraph.hpp"
#include "core/Log.hpp"

namespace violet {

void Tonemap::init(VulkanContext* ctx, MaterialManager* matMgr, DescriptorManager* descMgr,
                   RenderGraph* graph, const eastl::string& hdrName, const eastl::string& swapchainName) {
    context = ctx;
    materialManager = matMgr;
    descriptorManager = descMgr;
    renderGraph = graph;
    hdrImageName = hdrName;
    swapchainImageName = swapchainName;

    // Get PostProcess material from MaterialManager
    postProcessMaterial = materialManager->getMaterialByName("PostProcess");
    if (!postProcessMaterial) {
        violet::Log::error("Tonemap", "Failed to get PostProcess material from MaterialManager");
        return;
    }

    // For now, allocate descriptor sets using deprecated allocateSets API
    // TODO: Replace with reflection-based texture descriptor API once implemented
    auto sets = descriptorManager->allocateSets("PostProcess", MAX_FRAMES_IN_FLIGHT);
    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descriptorSets[i] = sets[i];
    }

    violet::Log::info("Tonemap", "Initialized with {} -> {} ({} frames)", hdrName.c_str(), swapchainName.c_str(), MAX_FRAMES_IN_FLIGHT);
}

void Tonemap::cleanup() {
    // Descriptor set managed by DescriptorManager, no manual cleanup needed
    postProcessMaterial = nullptr;
    descriptorManager = nullptr;
    materialManager = nullptr;
    context = nullptr;
}

void Tonemap::executePass(vk::CommandBuffer cmd, uint32_t frameIndex) {
    if (!postProcessMaterial || !postProcessMaterial->getPipeline()) {
        violet::Log::error("Tonemap", "PostProcess material or pipeline not available");
        return;
    }

    if (frameIndex >= descriptorSets.size()) {
        violet::Log::error("Tonemap", "Invalid frame index: {}", frameIndex);
        return;
    }

    // Get HDR and depth resources from RenderGraph
    const LogicalResource* hdrRes = renderGraph->getResource(hdrImageName);
    if (!hdrRes) {
        violet::Log::error("Tonemap", "HDR resource '{}' not found in RenderGraph", hdrImageName.c_str());
        return;
    }

    // Get depth resource (transient depth buffer from Main pass)
    const LogicalResource* depthRes = renderGraph->getResource("depth");
    if (!depthRes) {
        violet::Log::error("Tonemap", "Depth resource not found in RenderGraph");
        return;
    }

    // Get image views - check for null imageResource before accessing view
    vk::ImageView hdrView = (hdrRes->isExternal && hdrRes->imageResource) ?
        hdrRes->imageResource->view : hdrRes->transientView;
    vk::ImageView depthView = (depthRes->isExternal && depthRes->imageResource) ?
        depthRes->imageResource->view : depthRes->transientView;

    if (!hdrView || !depthView) {
        violet::Log::error("Tonemap", "Invalid HDR or depth image view");
        return;
    }

    // Update descriptor set for this frame (always update since RenderGraph rebuilds each frame)
    vk::DescriptorSet currentSet = descriptorSets[frameIndex];
    descriptorManager->updateSet(currentSet, {
        ResourceBindingDesc::sampledImage(0, hdrView, descriptorManager->getSampler(SamplerType::ClampToEdge)),
        ResourceBindingDesc::sampledImage(1, depthView, descriptorManager->getSampler(SamplerType::ClampToEdge))
    });

    // Get swapchain resource to determine viewport/scissor dimensions
    const LogicalResource* swapchainRes = renderGraph->getResource(swapchainImageName);
    if (!swapchainRes || !swapchainRes->imageResource) {
        violet::Log::error("Tonemap", "Swapchain resource not found");
        return;
    }

    // Set viewport (dynamic state)
    vk::Viewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainRes->imageResource->width);
    viewport.height = static_cast<float>(swapchainRes->imageResource->height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    cmd.setViewport(0, 1, &viewport);

    // Set scissor (dynamic state)
    vk::Rect2D scissor;
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = vk::Extent2D{swapchainRes->imageResource->width, swapchainRes->imageResource->height};
    cmd.setScissor(0, 1, &scissor);

    // Bind pipeline
    postProcessMaterial->getPipeline()->bind(cmd);

    // Bind PostProcess descriptor set (Set 0)
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        postProcessMaterial->getPipelineLayout(),
        0,  // Set 0
        1,  // Bind 1 set
        &currentSet,
        0, nullptr
    );

    // Push constants: ev100, gamma, tonemapMode, padding
    struct PushConstants {
        float ev100;
        float gamma;
        uint32_t tonemapMode;
        float padding;
    } push{params.ev100, params.gamma, static_cast<uint32_t>(params.mode), 0.0f};

    // Push constants must match the pipeline layout's stage flags (VERTEX|FRAGMENT)
    cmd.pushConstants(
        postProcessMaterial->getPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, sizeof(PushConstants), &push
    );

    // Draw fullscreen triangle (no vertex buffer needed)
    cmd.draw(3, 1, 0, 0);
}

} // namespace violet
