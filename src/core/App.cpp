#include "App.hpp"
#include "ui/UILayer.hpp"
#include "input/InputManager.hpp"
#include "renderer/ForwardRenderer.hpp"
#include "renderer/DebugRenderer.hpp"
#include "Exception.hpp"
#include <imgui.h>
#include <EASTL/array.h>

namespace violet {

App::App() : window(1280, 720, "Violet Engine") {
    window.setResizeCallback([this](int width, int height) {
        recreateSwapchain();
    });

    // Initialize input system with the window
    InputManager::initialize(window.getHandle());
}

App::~App() {
    if (!cleanedUp && context.getDevice()) {
        context.getDevice().waitIdle();
        internalCleanup();
    }
}

void App::run() {
    initVulkan();
    mainLoop();
}


void App::initVulkan() {
    frameTimer.reset();

    context.init(window.getHandle());
    swapchain.init(&context);

    // Initialize UI layer if set
    if (uiLayer) {
        uiLayer->onAttach(&context, window.getHandle());
    }

    createResources();

    // Initialize ImGui backend after renderer is set up
    if (forwardRenderer) {
        imguiBackend.init(&context, window.getHandle(), forwardRenderer->getFinalPassRenderPass(), MAX_FRAMES_IN_FLIGHT);
    }
    createCommandBuffers();
    createSyncObjects();
}

void App::mainLoop() {
    while (!window.shouldClose()) {
        deltaTime = frameTimer.tick();

        window.pollEvents();

        update(deltaTime);

        if (uiLayer) {
            uiLayer->onUpdate(deltaTime);
        }

        // Process input events
        InputManager::processEvents();

        drawFrame();
    }
    context.getDevice().waitIdle();
}

void App::createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = context.getCommandPool();
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    auto stdBuffers = context.getDevice().allocateCommandBuffers(allocInfo);
    for (size_t i = 0; i < stdBuffers.size(); i++) {
        commandBuffers[i] = stdBuffers[i];
    }
}

void App::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    vk::SemaphoreCreateInfo semaphoreInfo;
    vk::FenceCreateInfo fenceInfo;
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        imageAvailableSemaphores[i] = context.getDevice().createSemaphore(semaphoreInfo);
        renderFinishedSemaphores[i] = context.getDevice().createSemaphore(semaphoreInfo);
        inFlightFences[i] = context.getDevice().createFence(fenceInfo);
    }
}

void App::drawFrame() {
    // Wait for previous frame
    context.getDevice().waitForFences(1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    context.getDevice().resetFences(1, &inFlightFences[currentFrame]);

    // Acquire next image
    uint32_t imageIndex;
    if (!acquireNextImage(imageIndex)) {
        return; // Swapchain needs recreation
    }

    // Record command buffer
    commandBuffers[currentFrame].reset();
    vk::CommandBufferBeginInfo beginInfo{};
    commandBuffers[currentFrame].begin(beginInfo);

    // Call virtual render function
    renderFrame(commandBuffers[currentFrame], imageIndex, currentFrame);

    commandBuffers[currentFrame].end();

    // Submit and present
    submitAndPresent(imageIndex);

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void App::internalCleanup() {
    if (cleanedUp) return;

    cleanup();

    if (uiLayer) {
        uiLayer->onDetach();
    }

    imguiBackend.cleanup();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        context.getDevice().destroySemaphore(imageAvailableSemaphores[i]);
        context.getDevice().destroySemaphore(renderFinishedSemaphores[i]);
        context.getDevice().destroyFence(inFlightFences[i]);
    }

    swapchain.cleanup();
    context.cleanup();


    cleanedUp = true;
}

void App::renderFrame(vk::CommandBuffer cmd, uint32_t imageIndex, uint32_t frameIndex) {
    vk::Framebuffer framebuffer = swapchain.getFramebuffer(imageIndex);
    vk::Extent2D extent = swapchain.getExtent();

    // Scene rendering - use shared framebuffer
    if (forwardRenderer && world) {
        forwardRenderer->beginFrame(*world, frameIndex);
        forwardRenderer->renderFrame(cmd, framebuffer, extent, frameIndex);
        forwardRenderer->endFrame();
    }

    // Debug and UI rendering - use same framebuffer
    if (debugRenderer) {
        debugRenderer->renderDebugAndUI(cmd, framebuffer, extent, frameIndex);
    }
}

bool App::acquireNextImage(uint32_t& imageIndex) {
    try {
        imageIndex = swapchain.acquireNextImage(imageAvailableSemaphores[currentFrame]);
        return true;
    } catch (const vk::OutOfDateKHRError&) {
        recreateSwapchain();
        return false;
    }
}

void App::submitAndPresent(uint32_t imageIndex) {
    vk::Semaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::Semaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};

    vk::SubmitInfo submitInfo;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    context.getGraphicsQueue().submit(1, &submitInfo, inFlightFences[currentFrame]);
    swapchain.present(imageIndex, renderFinishedSemaphores[currentFrame]);
}

void App::recreateSwapchain() {
    // Wait for valid window size
    int width = 0, height = 0;
    do {
        window.getFramebufferSize(&width, &height);
        if (width == 0 || height == 0) {
            window.waitEvents();
        }
    } while (width == 0 || height == 0);

    context.getDevice().waitIdle();
    swapchain.recreate();

    // Recreate swapchain framebuffers
    if (forwardRenderer) {
        // Use final pass RenderPass for framebuffer creation
        swapchain.createFramebuffers(forwardRenderer->getFinalPassRenderPass());
    }

    onWindowResize(width, height);
}

}