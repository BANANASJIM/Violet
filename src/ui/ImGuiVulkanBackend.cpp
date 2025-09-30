#include "ImGuiVulkanBackend.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/Buffer.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include "core/Log.hpp"

namespace violet {

ImGuiVulkanBackend::~ImGuiVulkanBackend() {
    cleanup();
}

void ImGuiVulkanBackend::init(VulkanContext* ctx, GLFWwindow* window, vk::RenderPass renderPass, uint32_t imageCount) {
    context = ctx;

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup display size and scale
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

    io.DisplaySize = ImVec2((float)width, (float)height);
    if (width > 0 && height > 0) {
        io.DisplayFramebufferScale = ImVec2((float)fbWidth / width, (float)fbHeight / height);
    }

    // Ensure mouse input is enabled
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Create descriptor pool for ImGui
    createDescriptorPool();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window, true);
    
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = context->getInstance();
    init_info.PhysicalDevice = context->getPhysicalDevice();
    init_info.Device = context->getDevice();
    init_info.QueueFamily = context->getQueueFamilies().graphicsFamily.value();
    init_info.Queue = context->getGraphicsQueue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = descriptorPool;
    init_info.RenderPass = renderPass;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = imageCount;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;

    ImGui_ImplVulkan_Init(&init_info);

    uploadFonts();
    
    initialized = true;
    violet::Log::info("UI", "ImGui Vulkan backend initialized");
}

void ImGuiVulkanBackend::cleanup() {
    if (!initialized) return;

    context->getDevice().waitIdle();
    
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (descriptorPool) {
        context->getDevice().destroyDescriptorPool(descriptorPool);
        descriptorPool = nullptr;
    }

    initialized = false;
}

void ImGuiVulkanBackend::createDescriptorPool() {
    vk::DescriptorPoolSize pool_sizes[] = {
        { vk::DescriptorType::eSampler, 1000 },
        { vk::DescriptorType::eCombinedImageSampler, 1000 },
        { vk::DescriptorType::eSampledImage, 1000 },
        { vk::DescriptorType::eStorageImage, 1000 },
        { vk::DescriptorType::eUniformTexelBuffer, 1000 },
        { vk::DescriptorType::eStorageTexelBuffer, 1000 },
        { vk::DescriptorType::eUniformBuffer, 1000 },
        { vk::DescriptorType::eStorageBuffer, 1000 },
        { vk::DescriptorType::eUniformBufferDynamic, 1000 },
        { vk::DescriptorType::eStorageBufferDynamic, 1000 },
        { vk::DescriptorType::eInputAttachment, 1000 }
    };

    vk::DescriptorPoolCreateInfo pool_info;
    pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    descriptorPool = context->getDevice().createDescriptorPool(pool_info);
}

void ImGuiVulkanBackend::uploadFonts() {
    vk::CommandBuffer commandBuffer = beginSingleTimeCommands(context);
    ImGui_ImplVulkan_CreateFontsTexture();
    endSingleTimeCommands(context, commandBuffer);
}

}
