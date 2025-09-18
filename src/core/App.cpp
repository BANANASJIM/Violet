#include "App.hpp"
#include "ui/UILayer.hpp"
#include <imgui.h>
#include <EASTL/array.h>
#include <array>
#include <stdexcept>

namespace violet {

App::App() : window(1280, 720, "Violet Engine") {
    window.setResizeCallback([this](int width, int height) {
        recreateSwapchain();
    });

    // Initialize input system with the window
    Input::initialize(window.getHandle());
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
    lastFrameTime = std::chrono::high_resolution_clock::now();

    context.init(window.getHandle());
    swapchain.init(&context);
    renderPass.init(&context, swapchain.getImageFormat());

    // Initialize ImGui backend
    imguiBackend.init(&context, window.getHandle(), renderPass.getRenderPass(), MAX_FRAMES_IN_FLIGHT);

    // Initialize UI layer if set
    if (uiLayer) {
        uiLayer->onAttach(&context, window.getHandle());
    }

    createResources();

    swapchain.createFramebuffers(renderPass.getRenderPass());
    createCommandBuffers();
    createSyncObjects();
}

void App::mainLoop() {
    while (!window.shouldClose()) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        window.pollEvents();
        Input::update();

        update(deltaTime);

        if (uiLayer) {
            uiLayer->onUpdate(deltaTime);
        }

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
    context.getDevice().waitForFences(1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    context.getDevice().resetFences(1, &inFlightFences[currentFrame]);

    uint32_t imageIndex = swapchain.acquireNextImage(imageAvailableSemaphores[currentFrame]);

    commandBuffers[currentFrame].reset();

    vk::CommandBufferBeginInfo beginInfo;
    commandBuffers[currentFrame].begin(beginInfo);

    eastl::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.renderPass = renderPass.getRenderPass();
    renderPassInfo.framebuffer = swapchain.getFramebuffer(imageIndex);
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = swapchain.getExtent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    commandBuffers[currentFrame].beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    updateUniforms(currentFrame);
    recordCommands(commandBuffers[currentFrame], imageIndex);

    // Render ImGui
    if (uiLayer) {
        uiLayer->beginFrame();
        uiLayer->onImGuiRender();
        uiLayer->endFrame(commandBuffers[currentFrame]);
    }

    commandBuffers[currentFrame].endRenderPass();
    commandBuffers[currentFrame].end();

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

    renderPass.cleanup();
    swapchain.cleanup();
    context.cleanup();


    cleanedUp = true;
}

void App::recreateSwapchain() {
    int width = 0, height = 0;
    window.getFramebufferSize(&width, &height);
    while (width == 0 || height == 0) {
        window.getFramebufferSize(&width, &height);
        window.waitEvents();
    }

    context.getDevice().waitIdle();

    swapchain.recreate();
    swapchain.createFramebuffers(renderPass.getRenderPass());
    onWindowResize(width, height);
}

}