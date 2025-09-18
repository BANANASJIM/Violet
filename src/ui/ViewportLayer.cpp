#include "ViewportLayer.hpp"
#include <imgui.h>
#include "core/Log.hpp"

namespace violet {

void ViewportLayer::onAttach(VulkanContext* context, GLFWwindow* window) {
    UILayer::onAttach(context, window);
    initialized = true;
}

void ViewportLayer::onDetach() {
    UILayer::onDetach();
}

void ViewportLayer::onImGuiRender() {
    ImGui::Begin("Viewport");

    ImVec2 windowSize = ImGui::GetWindowSize();
    ImVec2 textSize = ImGui::CalcTextSize(statusMessage.c_str());
    ImGui::SetCursorPos(ImVec2((windowSize.x - textSize.x) * 0.5f, (windowSize.y - textSize.y) * 0.5f));
    ImGui::Text("%s", statusMessage.c_str());

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const char* path = (const char*)payload->Data;
            VT_INFO("Asset dropped in viewport: {}", path);

            if (onAssetDropped) {
                onAssetDropped(path);
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
}

}