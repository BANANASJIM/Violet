#pragma once

#include "Window.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/Swapchain.hpp"
#include "renderer/RenderPass.hpp"
#include "input/Input.hpp"
#include "ui/ImGuiVulkanBackend.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <chrono>

namespace violet {

class UILayer;

class App {
public:
    App();
    virtual ~App();

    void run();

protected:
    virtual void createResources() = 0;
    virtual void updateUniforms(uint32_t frameIndex) = 0;
    virtual void recordCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) = 0;
    virtual void update(float deltaTime) {}
    virtual void onWindowResize(int width, int height) {}
    virtual void cleanup() {}

    void setUILayer(UILayer* layer) { uiLayer = layer; }
    UILayer* getUILayer() { return uiLayer; }

    VulkanContext* getContext() { return &context; }
    Swapchain* getSwapchain() { return &swapchain; }
    RenderPass* getRenderPass() { return &renderPass; }
    uint32_t getCurrentFrame() const { return currentFrame; }
    GLFWwindow* getWindow() { return window.getHandle(); }

private:
    void initVulkan();
    void mainLoop();
    void createCommandBuffers();
    void createSyncObjects();
    void drawFrame();
    void recreateSwapchain();
    void internalCleanup();

protected:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

private:
    Window window;
    VulkanContext context;
    Swapchain swapchain;
    RenderPass renderPass;

    eastl::vector<vk::Semaphore> imageAvailableSemaphores;
    eastl::vector<vk::Semaphore> renderFinishedSemaphores;
    eastl::vector<vk::Fence> inFlightFences;
    eastl::vector<vk::CommandBuffer> commandBuffers;

    ImGuiVulkanBackend imguiBackend;
    UILayer* uiLayer{nullptr};

    uint32_t currentFrame = 0;
    bool cleanedUp = false;

    std::chrono::high_resolution_clock::time_point lastFrameTime;
    float deltaTime = 0.0f;
};

}