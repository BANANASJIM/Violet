#include "RenderSettingsLayer.hpp"
#include "renderer/ForwardRenderer.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include <imgui.h>

namespace violet {

RenderSettingsLayer::RenderSettingsLayer(ForwardRenderer* renderer)
    : renderer(renderer) {
}

void RenderSettingsLayer::onAttach(VulkanContext* context, GLFWwindow* window) {
    UILayer::onAttach(context, window);

    // Get device capabilities and initialize settings
    settings = RenderSettings::getDefaults(context->getPhysicalDevice());
    maxDeviceAnisotropy = settings.maxAnisotropy;
}

void RenderSettingsLayer::onImGuiRender() {
    ImGui::Begin("Render Settings");

    // Display read-only settings (loaded from config.json at startup)
    ImGui::SeparatorText("Texture Quality");
    ImGui::Text("Settings are loaded from config.json");
    ImGui::Text("Restart the application to apply changes");
    ImGui::Spacing();

    ImGui::BeginDisabled(true);
    ImGui::Checkbox("Anisotropic Filtering", &settings.enableAnisotropy);
    ImGui::SliderFloat("Max Anisotropy", &settings.maxAnisotropy, 1.0f, maxDeviceAnisotropy, "%.0fx");
    ImGui::EndDisabled();

    // MSAA Section (placeholder - not yet functional)
    ImGui::SeparatorText("Anti-Aliasing");

    const char* msaaOptions[] = { "Off (1x)", "MSAA 2x", "MSAA 4x", "MSAA 8x" };
    int currentMSAA = 0;
    switch (settings.msaaSamples) {
        case vk::SampleCountFlagBits::e1: currentMSAA = 0; break;
        case vk::SampleCountFlagBits::e2: currentMSAA = 1; break;
        case vk::SampleCountFlagBits::e4: currentMSAA = 2; break;
        case vk::SampleCountFlagBits::e8: currentMSAA = 3; break;
        default: currentMSAA = 0;
    }

    ImGui::BeginDisabled(true);  // Disabled - not yet implemented
    ImGui::Combo("MSAA", &currentMSAA, msaaOptions, IM_ARRAYSIZE(msaaOptions));
    ImGui::EndDisabled();
    ImGui::TextDisabled("(MSAA implementation pending)");

    ImGui::End();
}

}
