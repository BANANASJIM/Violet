#include "App.hpp"
#include "ui/UILayer.hpp"
#include "input/InputManager.hpp"
#include "renderer/ForwardRenderer.hpp"
#include "renderer/DebugRenderer.hpp"
#include "Exception.hpp"
#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include <imgui.h>
#include <EASTL/array.h>

#define JSON_HAS_CPP_17
#include <nlohmann/json.hpp>
#include <fstream>

namespace violet {

static Window createWindow() {
    int width = 1920, height = 1080;

    eastl::string resolvedPath = violet::FileSystem::resolveRelativePath("config.json");
    std::ifstream configFile(resolvedPath.c_str());
    if (configFile.is_open()) {
        nlohmann::json config;
        configFile >> config;
        if (config.contains("window")) {
            auto& windowConfig = config["window"];
            if (windowConfig.contains("width")) width = windowConfig["width"].get<int>();
            if (windowConfig.contains("height")) height = windowConfig["height"].get<int>();
            violet::Log::info("App", "Loaded window size from config: {}x{}", width, height);
        }
    }

    return Window(width, height, "Violet Engine");
}

App::App() : window(createWindow()) {
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

    // Set swapchain for RenderGraph (after renderer initialized)
    if (forwardRenderer) {
        forwardRenderer->setSwapchain(&swapchain);
    }

    // TODO: ImGui backend needs update for dynamic rendering
    // if (forwardRenderer) {
    //     imguiBackend.init(&context, window.getHandle(), swapchainFormat, MAX_FRAMES_IN_FLIGHT);
    // }
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
    uint32_t imageCount = static_cast<uint32_t>(swapchain.getImageCount());
    imageAvailableSemaphores.resize(imageCount);
    renderFinishedSemaphores.resize(imageCount);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    vk::SemaphoreCreateInfo semaphoreInfo;
    vk::FenceCreateInfo fenceInfo;
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    for (size_t i = 0; i < imageCount; i++) {
        imageAvailableSemaphores[i] = context.getDevice().createSemaphore(semaphoreInfo);
        renderFinishedSemaphores[i] = context.getDevice().createSemaphore(semaphoreInfo);
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        inFlightFences[i] = context.getDevice().createFence(fenceInfo);
    }
}

void App::drawFrame() {
// Wait for previous frame
    auto waitResult = context.getDevice().waitForFences(1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    // Check wait result
    if (waitResult != vk::Result::eSuccess) {
        violet::Log::error("App", "Failed to wait for fence");
        return;
    }

    // Acquire next image (do this BEFORE resetting fence)
    uint32_t imageIndex;
    if (!acquireNextImage(imageIndex)) {
        return; // Swapchain needs recreation, fence still signaled for next frame
    }

    // Only reset fence after successfully acquiring image
    if (context.getDevice().resetFences(1, &inFlightFences[currentFrame]) != vk::Result::eSuccess) {
        violet::Log::error("App", "Failed to reset fence");
        return;
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

    for (auto& semaphore : imageAvailableSemaphores) {
        context.getDevice().destroySemaphore(semaphore);
    }
    for (auto& semaphore : renderFinishedSemaphores) {
        context.getDevice().destroySemaphore(semaphore);
    }
    for (auto& fence : inFlightFences) {
        context.getDevice().destroyFence(fence);
    }

    swapchain.cleanup();
    context.cleanup();


    cleanedUp = true;
}

void App::renderFrame(vk::CommandBuffer cmd, uint32_t imageIndex, uint32_t frameIndex) {
    vk::Extent2D extent = swapchain.getExtent();

    // Scene rendering - RenderGraph manages barriers
    if (forwardRenderer && world) {
        forwardRenderer->beginFrame(*world, frameIndex);
        forwardRenderer->renderFrame(cmd, imageIndex, extent, frameIndex);
        forwardRenderer->endFrame();
    }

    // TODO: Debug and UI rendering needs update for dynamic rendering
    // if (debugRenderer) {
    //     debugRenderer->renderDebugAndUI(cmd, extent, frameIndex);
    // }
}

bool App::acquireNextImage(uint32_t& imageIndex) {
    try {
        auto result = context.getDevice().acquireNextImageKHR(swapchain.getSwapchain(), UINT64_MAX, imageAvailableSemaphores[currentFrame]);
        imageIndex = result.value;
        return true;
    } catch (const vk::OutOfDateKHRError&) {
        recreateSwapchain();
        return false;
    }
}

void App::submitAndPresent(uint32_t imageIndex) {
    vk::Semaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::Semaphore signalSemaphores[] = {renderFinishedSemaphores[imageIndex]};

    vk::SubmitInfo submitInfo;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (context.getGraphicsQueue().submit(1, &submitInfo, inFlightFences[currentFrame]) != vk::Result::eSuccess) {
        violet::Log::error("App", "Failed to submit command buffer");
        return;
    }

    vk::SwapchainKHR swapchainHandle = swapchain.getSwapchain();
    vk::PresentInfoKHR presentInfo(1, &signalSemaphores[0], 1, &swapchainHandle, &imageIndex);

    VkResult result = vkQueuePresentKHR(context.getPresentQueue(), reinterpret_cast<const VkPresentInfoKHR*>(&presentInfo));
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    }
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

    vk::Extent2D newExtent = swapchain.getExtent();

    if (forwardRenderer) {
        forwardRenderer->onSwapchainRecreate(newExtent);
        // RenderGraph will rebuild automatically on next frame
    }

    onWindowResize(width, height);
}

}