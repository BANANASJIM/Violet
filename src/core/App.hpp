#pragma once

#include "Window.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/Swapchain.hpp"
#include "renderer/graph/RenderPass.hpp"
#include "ui/ImGuiVulkanBackend.hpp"
#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <entt/entt.hpp>
#include "Timer.hpp"

namespace violet {

class UILayer;
class ForwardRenderer;
class DebugRenderer;

class App {
public:
    App();
    virtual ~App();

    void run();

protected:
    virtual void createResources() = 0;
    virtual void update(float deltaTime) {}
    virtual void onWindowResize(int width, int height) {}
    virtual void cleanup() {}

    // New simplified rendering interface
    virtual void renderFrame(vk::CommandBuffer cmd, uint32_t imageIndex, uint32_t frameIndex);

    // Renderer configuration for subclasses
    class ForwardRenderer* forwardRenderer = nullptr;
    class DebugRenderer* debugRenderer = nullptr;
    entt::registry* world = nullptr;

    void setUILayer(UILayer* layer) { uiLayer = layer; }
    UILayer* getUILayer() { return uiLayer; }

    VulkanContext* getContext() { return &context; }
    Swapchain* getSwapchain() { return &swapchain; }
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

    // Simplified helper methods
    bool acquireNextImage(uint32_t& imageIndex);
    void submitAndPresent(uint32_t imageIndex);

protected:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

private:
    Window window;
    VulkanContext context;
    Swapchain swapchain;

    eastl::vector<vk::Semaphore> imageAvailableSemaphores;
    eastl::vector<vk::Semaphore> renderFinishedSemaphores;
    eastl::vector<vk::Fence> inFlightFences;
    eastl::vector<vk::CommandBuffer> commandBuffers;

    ImGuiVulkanBackend imguiBackend;
    UILayer* uiLayer{nullptr};

    uint32_t currentFrame = 0;
    bool cleanedUp = false;

    Timer frameTimer;
    float deltaTime = 0.0f;
};

}