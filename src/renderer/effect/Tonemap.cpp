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
                   RenderGraph* graph, const eastl::string& hdrName, const eastl::string& swapchainName,
                   ShaderLibrary* shaderLib) {
    context = ctx;
    materialManager = matMgr;
    descriptorManager = descMgr;
    renderGraph = graph;
    hdrImageName = hdrName;
    swapchainImageName = swapchainName;

    postProcessMaterial = materialManager->getMaterialByName("PostProcess");
    if (!postProcessMaterial) {
        violet::Log::error("Tonemap", "Failed to get PostProcess material from MaterialManager");
        return;
    }

    violet::Log::info("Tonemap", "Initialized with {} -> {}", hdrName.c_str(), swapchainName.c_str());
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

    // Get HDR and depth resources from RenderGraph
    const LogicalResource* hdrRes = renderGraph->getResource(hdrImageName);
    if (!hdrRes) {
        violet::Log::error("Tonemap", "HDR resource '{}' not found in RenderGraph", hdrImageName.c_str());
        return;
    }

    const LogicalResource* depthRes = renderGraph->getResource("depth");
    if (!depthRes) {
        violet::Log::error("Tonemap", "Depth resource not found in RenderGraph");
        return;
    }

    // Get image views
    vk::ImageView hdrView = (hdrRes->isExternal && hdrRes->imageResource) ?
        hdrRes->imageResource->view : hdrRes->transientView;
    vk::ImageView depthView = (depthRes->isExternal && depthRes->imageResource) ?
        depthRes->imageResource->view : depthRes->transientView;

    if (!hdrView || !depthView) {
        violet::Log::error("Tonemap", "Invalid HDR or depth image view");
        return;
    }

    // Update resources (reusing member variables, not creating new ones every frame)
    // Binding 0: colorTexture, Binding 1: depthTexture, Binding 2: texSampler
    vk::Sampler clampSampler = descriptorManager->getSamplerManager().getSampler(SamplerType::ClampToEdge);
    resources.clear();  // Clear previous frame's data
    resources.bindSampledImage(0, 0, hdrView);      // Set 0, Binding 0: colorTexture
    resources.bindSampledImage(0, 1, depthView);    // Set 0, Binding 1: depthTexture
    resources.bindSampler(0, 2, clampSampler);      // Set 0, Binding 2: texSampler

    // TODO: Phase 3 - Implement DescriptorManager::update/bind API
    // descriptorManager->update(resources, postProcessMaterial->getPipeline());
    // descriptorManager->bind(cmd, postProcessMaterial->getPipeline());

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

    // Push constants: ev100, gamma, tonemapMode, padding
    struct PushConstants {
        float ev100;
        float gamma;
        uint32_t tonemapMode;
        float padding;
    } push{params.ev100, params.gamma, static_cast<uint32_t>(params.mode), 0.0f};

    // Push constants must match the pipeline layout's stage flags
    // NOTE: Includes COMPUTE_BIT because luminance_histogram shader shares the same push constant layout
    cmd.pushConstants(
        postProcessMaterial->getPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
        0, sizeof(PushConstants), &push
    );

    // Draw fullscreen triangle (no vertex buffer needed)
    cmd.draw(3, 1, 0, 0);
}

} // namespace violet
