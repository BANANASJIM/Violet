#pragma once

#include "renderer/VulkanContext.hpp"
#include "renderer/Swapchain.hpp"
#include "renderer/RenderPass.hpp"
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>

namespace violet {

class App {
public:
    virtual ~App();

    void run();

protected:
    virtual void createResources() = 0;
    virtual void updateUniforms(uint32_t frameIndex) = 0;
    virtual void recordCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) = 0;
    virtual void cleanup() {}

    VulkanContext* getContext() { return &context; }
    Swapchain* getSwapchain() { return &swapchain; }
    RenderPass* getRenderPass() { return &renderPass; }
    uint32_t getCurrentFrame() const { return currentFrame; }

private:
    void initWindow();
    void initVulkan();
    void mainLoop();
    void createCommandBuffers();
    void createSyncObjects();
    void drawFrame();
    void internalCleanup();

protected:
    static constexpr uint32_t WIDTH = 1280;
    static constexpr uint32_t HEIGHT = 720;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

private:
    GLFWwindow* window = nullptr;
    VulkanContext context;
    Swapchain swapchain;
    RenderPass renderPass;

    eastl::vector<vk::Semaphore> imageAvailableSemaphores;
    eastl::vector<vk::Semaphore> renderFinishedSemaphores;
    eastl::vector<vk::Fence> inFlightFences;
    eastl::vector<vk::CommandBuffer> commandBuffers;

    uint32_t currentFrame = 0;
    bool cleanedUp = false;
};

}