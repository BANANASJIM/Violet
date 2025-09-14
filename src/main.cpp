#include "renderer/VulkanContext.hpp"
#include "renderer/Swapchain.hpp"
#include "renderer/RenderPass.hpp"
#include "renderer/Pipeline.hpp"
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include <EASTL/vector.h>
#include <array>

// EASTL allocator implementation
void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line) {
    return malloc(size);
}

void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line) {
    return aligned_alloc(alignment, size);
}

namespace violet {

class Application {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Violet Engine", nullptr, nullptr);
    }

    void initVulkan() {
        context.init(window);
        swapchain.init(&context);
        renderPass.init(&context, swapchain.getImageFormat());
        pipeline.init(&context, &renderPass, "build/shaders/triangle.vert.spv", "build/shaders/triangle.frag.spv");
        swapchain.createFramebuffers(renderPass.getRenderPass());
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
        }
        context.getDevice().waitIdle();
    }

    void cleanup() {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            context.getDevice().destroySemaphore(imageAvailableSemaphores[i]);
            context.getDevice().destroySemaphore(renderFinishedSemaphores[i]);
            context.getDevice().destroyFence(inFlightFences[i]);
        }
        
        context.getDevice().destroyCommandPool(commandPool);
        pipeline.cleanup();
        renderPass.cleanup();
        swapchain.cleanup();
        context.cleanup();
        
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void createCommandPool() {
        vk::CommandPoolCreateInfo poolInfo;
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = context.getQueueFamilies().graphicsFamily.value();
        
        commandPool = context.getDevice().createCommandPool(poolInfo);
    }

    void createCommandBuffers() {
        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        
        vk::CommandBufferAllocateInfo allocInfo;
        allocInfo.commandPool = commandPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        
        auto stdBuffers = context.getDevice().allocateCommandBuffers(allocInfo);
        for (size_t i = 0; i < stdBuffers.size(); i++) {
            commandBuffers[i] = stdBuffers[i];
        }
    }

    void createSyncObjects() {
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

    void drawFrame() {
        context.getDevice().waitForFences(1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        context.getDevice().resetFences(1, &inFlightFences[currentFrame]);
        
        uint32_t imageIndex = swapchain.acquireNextImage(imageAvailableSemaphores[currentFrame]);
        
        commandBuffers[currentFrame].reset();
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
        
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

    void recordCommandBuffer(vk::CommandBuffer commandBuffer, uint32_t imageIndex) {
        vk::CommandBufferBeginInfo beginInfo;
        commandBuffer.begin(beginInfo);

        vk::RenderPassBeginInfo renderPassInfo;
        renderPassInfo.renderPass = renderPass.getRenderPass();
        renderPassInfo.framebuffer = swapchain.getFramebuffers()[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = swapchain.getExtent();

        vk::ClearValue clearColor;
        clearColor.color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());

        vk::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchain.getExtent().width);
        viewport.height = static_cast<float>(swapchain.getExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        commandBuffer.setViewport(0, 1, &viewport);

        vk::Rect2D scissor;
        scissor.offset = vk::Offset2D{0, 0};
        scissor.extent = swapchain.getExtent();
        commandBuffer.setScissor(0, 1, &scissor);

        commandBuffer.draw(3, 1, 0, 0);
        commandBuffer.endRenderPass();
        commandBuffer.end();
    }

private:
    GLFWwindow* window;
    VulkanContext context;
    Swapchain swapchain;
    RenderPass renderPass;
    Pipeline pipeline;
    
    vk::CommandPool commandPool;
    eastl::vector<vk::CommandBuffer> commandBuffers;
    
    eastl::vector<vk::Semaphore> imageAvailableSemaphores;
    eastl::vector<vk::Semaphore> renderFinishedSemaphores;
    eastl::vector<vk::Fence> inFlightFences;
    
    uint32_t currentFrame = 0;
    
    static constexpr uint32_t WIDTH = 1280;
    static constexpr uint32_t HEIGHT = 720;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
};

}

int main() {
    spdlog::set_level(spdlog::level::debug);
    
    violet::Application app;
    try {
        app.run();
    } catch (const std::exception& e) {
        spdlog::critical("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}