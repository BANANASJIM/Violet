#include "renderer/VulkanContext.hpp"
#include "renderer/Swapchain.hpp"
#include "renderer/RenderPass.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/Vertex.hpp"
#include "core/TestData.hpp"
#include "core/TestTexture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include <EASTL/vector.h>
#include <EASTL/array.h>
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
    ~Application() {
        if (!cleanedUp && context.getDevice()) {
            context.getDevice().waitIdle();
            cleanup();
        }
    }

    void run() {
        initWindow();
        initVulkan();
        mainLoop();
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

        // Create test resources
        createTestResources();

        // Create descriptor sets
        descriptorSet.create(&context, MAX_FRAMES_IN_FLIGHT);

        // Create pipeline with new shaders
        pipeline.init(&context, &renderPass, &descriptorSet, "build/shaders/model.vert.spv", "build/shaders/model.frag.spv");
        swapchain.createFramebuffers(renderPass.getRenderPass());

        // Setup descriptor sets
        setupDescriptorSets();

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
        if (cleanedUp) return;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            context.getDevice().destroySemaphore(imageAvailableSemaphores[i]);
            context.getDevice().destroySemaphore(renderFinishedSemaphores[i]);
            context.getDevice().destroyFence(inFlightFences[i]);
            uniformBuffers[i].cleanup();
        }

        cubeVertexBuffer.cleanup();
        cubeIndexBuffer.cleanup();
        testTexture.cleanup();
        descriptorSet.cleanup();

        pipeline.cleanup();
        renderPass.cleanup();
        swapchain.cleanup();
        context.cleanup();

        glfwDestroyWindow(window);
        glfwTerminate();

        cleanedUp = true;
    }


    void createCommandBuffers() {
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

        eastl::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

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

        // Bind vertex buffer
        vk::Buffer vertexBuffers[] = {cubeVertexBuffer.getBuffer()};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        // Bind index buffer
        commandBuffer.bindIndexBuffer(cubeIndexBuffer.getBuffer(), 0, vk::IndexType::eUint32);

        // Update uniform buffer
        updateUniformBuffer(currentFrame);

        // Bind descriptor set
        vk::DescriptorSet currentDescriptorSet = descriptorSet.getDescriptorSet(currentFrame);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.getLayout(),
                                         0, 1, &currentDescriptorSet, 0, nullptr);

        // Push constants
        PushConstants pushConstants{};
        pushConstants.baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        pushConstants.hasTexture = 1;
        commandBuffer.pushConstants(pipeline.getLayout(), vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(PushConstants), &pushConstants);

        // Draw indexed
        commandBuffer.drawIndexed(cubeIndexBuffer.getIndexCount(), 1, 0, 0, 0);
        commandBuffer.endRenderPass();
        commandBuffer.end();
    }

    void createTestResources() {
        // Create test cube
        auto vertices = TestData::getCubeVertices();
        auto indices = TestData::getCubeIndices();

        cubeVertexBuffer.create(&context, vertices);
        cubeIndexBuffer.create(&context, indices);

        // Create test texture
        TestTexture::createCheckerboardTexture(&context, testTexture);

        // Create uniform buffers
        uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            uniformBuffers[i].create(&context, sizeof(UniformBufferObject));
        }
    }

    void setupDescriptorSets() {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            descriptorSet.updateBuffer(i, &uniformBuffers[i]);
            descriptorSet.updateTexture(i, &testTexture);
        }
    }

    void updateUniformBuffer(uint32_t currentImage) {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo{};
        ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = glm::lookAt(glm::vec3(3.0f, 3.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), swapchain.getExtent().width / (float) swapchain.getExtent().height, 0.1f, 10.0f);

        // GLM was designed for OpenGL, where Y is flipped
        ubo.proj[1][1] *= -1;

        uniformBuffers[currentImage].update(&ubo, sizeof(ubo));
    }

private:
    // Window must be first (destroyed last)
    GLFWwindow* window = nullptr;

    // VulkanContext must be second (destroyed second to last)
    VulkanContext context;

    // All Vulkan resources (destroyed before context)
    eastl::vector<vk::Semaphore> imageAvailableSemaphores;
    eastl::vector<vk::Semaphore> renderFinishedSemaphores;
    eastl::vector<vk::Fence> inFlightFences;
    eastl::vector<vk::CommandBuffer> commandBuffers;

    // Higher level resources (destroyed before raw Vulkan resources)
    eastl::vector<UniformBuffer> uniformBuffers;
    Texture testTexture;
    VertexBuffer cubeIndexBuffer;
    VertexBuffer cubeVertexBuffer;
    DescriptorSet descriptorSet;
    Pipeline pipeline;
    RenderPass renderPass;
    Swapchain swapchain;
    
    uint32_t currentFrame = 0;
    bool cleanedUp = false;

    static constexpr uint32_t WIDTH = 1280;
    static constexpr uint32_t HEIGHT = 720;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
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