#include "UILayer.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>

namespace violet {

void UILayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    // Update display size each frame to handle scaling
    ImGuiIO& io = ImGui::GetIO();
    GLFWwindow* window = glfwGetCurrentContext();
    if (window) {
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

        io.DisplaySize = ImVec2((float)width, (float)height);
        if (width > 0 && height > 0) {
            io.DisplayFramebufferScale = ImVec2((float)fbWidth / width, (float)fbHeight / height);
        }
    }

    ImGui::NewFrame();
}

void UILayer::endFrame(vk::CommandBuffer commandBuffer) {
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
}

}
