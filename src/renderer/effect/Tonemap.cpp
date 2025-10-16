#include "Tonemap.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "resource/MaterialManager.hpp"
#include "resource/Material.hpp"
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

    // Allocate descriptor sets for HDR color and depth textures (one per frame in flight)
    constexpr uint32_t framesInFlight = 3;
    auto sets = descriptorManager->allocateSets("PostProcess", framesInFlight);
    descriptorSets.resize(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        descriptorSets[i] = sets[i];
    }

    violet::Log::info("Tonemap", "Initialized with {} -> {} ({} frames)", hdrName.c_str(), swapchainName.c_str(), framesInFlight);
}

void Tonemap::cleanup() {
    // Descriptor set managed by DescriptorManager, no manual cleanup needed
    postProcessMaterial = nullptr;
    descriptorManager = nullptr;
    materialManager = nullptr;
    context = nullptr;
}

void Tonemap::addToRenderGraph() {
    if (!renderGraph) return;

    renderGraph->addPass("Tonemap", [this](RenderGraph::PassBuilder& b, RenderPass& p) {
        b.read(hdrImageName, ResourceUsage::ShaderRead);
        b.read("depth", ResourceUsage::ShaderRead);  // Read depth from Main pass
        b.write(swapchainImageName, ResourceUsage::Present);  // Present to swapchain (auto post-barrier to PresentSrcKHR)
        b.execute([this](vk::CommandBuffer cmd, uint32_t frame) { executePass(cmd, frame); });
    });
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
