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

    // Allocate descriptor set for HDR color and depth textures
    descriptorSet = descriptorManager->allocateSet("PostProcess", 0);

    violet::Log::info("Tonemap", "Initialized with {} -> {}", hdrName.c_str(), swapchainName.c_str());
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

    renderGraph->addPass("Tonemap")
        .read(hdrImageName, ResourceUsage::ShaderRead)
        .write(swapchainImageName, ResourceUsage::ColorAttachment)
        .execute([this](vk::CommandBuffer cmd, uint32_t frame) { executePass(cmd, frame); });
}

void Tonemap::executePass(vk::CommandBuffer cmd, uint32_t frameIndex) {
    if (!postProcessMaterial || !postProcessMaterial->getPipeline()) {
        violet::Log::error("Tonemap", "PostProcess material or pipeline not available");
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

    vk::ImageView hdrView = hdrRes->isExternal ? hdrRes->imageResource->view : hdrRes->transientView;
    vk::ImageView depthView = depthRes->isExternal ? depthRes->imageResource->view : depthRes->transientView;

    if (!hdrView || !depthView) {
        violet::Log::error("Tonemap", "Invalid HDR or depth image view");
        return;
    }

    // Update descriptor set if views changed
    if (hdrView != cachedHDRView || depthView != cachedDepthView) {
        descriptorManager->updateSet(descriptorSet, {
            ResourceBindingDesc::sampledImage(0, hdrView, descriptorManager->getSampler(SamplerType::ClampToEdge)),
            ResourceBindingDesc::sampledImage(1, depthView, descriptorManager->getSampler(SamplerType::ClampToEdge))
        });
        cachedHDRView = hdrView;
        cachedDepthView = depthView;
    }

    // Bind pipeline
    postProcessMaterial->getPipeline()->bind(cmd);

    // Bind descriptor sets (set 0: Global, set 1: PostProcess textures)
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        postProcessMaterial->getPipelineLayout(),
        1,  // Start at set 1 (PostProcess)
        1,  // Bind 1 set
        &descriptorSet,
        0, nullptr
    );

    // Push constants: ev100, gamma, tonemapMode, padding
    struct PushConstants {
        float ev100;
        float gamma;
        uint32_t tonemapMode;
        float padding;
    } push{params.ev100, params.gamma, static_cast<uint32_t>(params.mode), 0.0f};

    cmd.pushConstants(
        postProcessMaterial->getPipelineLayout(),
        vk::ShaderStageFlagBits::eFragment,
        0, sizeof(PushConstants), &push
    );

    // Draw fullscreen triangle (no vertex buffer needed)
    cmd.draw(3, 1, 0, 0);
}

} // namespace violet
