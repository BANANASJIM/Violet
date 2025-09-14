#include "App.hpp"
#include <GLFW/glfw3.h>
#include <EASTL/array.h>
#include <array>
#include <stdexcept>

namespace violet {

App::~App() {
    if (!cleanedUp && context.getDevice()) {
        context.getDevice().waitIdle();
        internalCleanup();
    }
}

void App::run() {
    initWindow();
    initVulkan();
    mainLoop();
}

void App::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Violet Engine", nullptr, nullptr);
}

void App::initVulkan() {
    context.init(window);
    swapchain.init(&context);
    renderPass.init(&context, swapchain.getImageFormat());

    createResources();

    swapchain.createFramebuffers(renderPass.getRenderPass());
    createCommandBuffers();
    createSyncObjects();
}

void App::mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
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
    renderPassInfo.framebuffer = swapchain.getFramebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = swapchain.getExtent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    commandBuffers[currentFrame].beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    vk::Viewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain.getExtent().width);
    viewport.height = static_cast<float>(swapchain.getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commandBuffers[currentFrame].setViewport(0, 1, &viewport);

    vk::Rect2D scissor;
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = swapchain.getExtent();
    commandBuffers[currentFrame].setScissor(0, 1, &scissor);

    updateUniforms(currentFrame);
    recordCommands(commandBuffers[currentFrame], imageIndex);

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

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        context.getDevice().destroySemaphore(imageAvailableSemaphores[i]);
        context.getDevice().destroySemaphore(renderFinishedSemaphores[i]);
        context.getDevice().destroyFence(inFlightFences[i]);
    }

    renderPass.cleanup();
    swapchain.cleanup();
    context.cleanup();

    glfwDestroyWindow(window);
    glfwTerminate();

    cleanedUp = true;
}

}