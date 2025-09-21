#include "SceneDebugLayer.hpp"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

namespace violet {

void SceneDebugLayer::onImGuiRender() {
    ImGui::Begin("Scene Debug");

    if (ImGui::CollapsingHeader("Camera")) {
        auto cameraView = world->view<CameraComponent>();
        for (auto entity : cameraView) {
            auto& camera = cameraView.get<CameraComponent>(entity);
            if (camera.isActive && camera.camera) {
                glm::vec3 pos = camera.camera->getPosition();
                ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
            }
        }
    }

    if (ImGui::CollapsingHeader("Entities", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("EntityTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("World Position");
            ImGui::TableSetupColumn("Local Position");
            ImGui::TableSetupColumn("Scale");
            ImGui::TableHeadersRow();

            auto transformView = world->view<TransformComponent>();
            for (auto entity : transformView) {
                auto& transform = transformView.get<TransformComponent>(entity);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%u", static_cast<uint32_t>(entity));

                ImGui::TableNextColumn();
                glm::vec3 worldPos = transform.world.position;
                ImGui::Text("%.1f, %.1f, %.1f", worldPos.x, worldPos.y, worldPos.z);

                ImGui::TableNextColumn();
                glm::vec3 localPos = transform.local.position;
                ImGui::Text("%.1f, %.1f, %.1f", localPos.x, localPos.y, localPos.z);

                ImGui::TableNextColumn();
                glm::vec3 scale = transform.local.scale;
                ImGui::Text("%.1f, %.1f, %.1f", scale.x, scale.y, scale.z);
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Stats")) {
        size_t totalEntities = world->getEntityCount();
        auto transformView = world->view<TransformComponent>();
        auto meshView = world->view<MeshComponent>();

        ImGui::Text("Total Entities: %zu", totalEntities);
        ImGui::Text("With Transform: %zu", transformView.size());
        ImGui::Text("With Mesh: %zu", meshView.size());
    }

    ImGui::End();
}

}