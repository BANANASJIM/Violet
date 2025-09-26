#include "SceneDebugLayer.hpp"
#include "input/InputManager.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "renderer/ForwardRenderer.hpp"
#include "ecs/Components.hpp"
#include "math/Ray.hpp"
#include "scene/Scene.hpp"
#include "scene/Node.hpp"

namespace violet {

// Global pointer to current SceneDebugLayer for renderer access
SceneDebugLayer* g_currentSceneDebugLayer = nullptr;

SceneDebugLayer::~SceneDebugLayer() {
}

void SceneDebugLayer::onAttach(VulkanContext* context, GLFWwindow* window) {
    UILayer::onAttach(context, window);
    initialize();
}

void SceneDebugLayer::onDetach() {
    shutdown();
    UILayer::onDetach();
}

void SceneDebugLayer::initialize() {

    // Set global pointer
    g_currentSceneDebugLayer = this;

    // Subscribe to mouse click events with high priority
    mouseClickHandlerId = EventDispatcher::subscribe<MousePressedEvent>(
        [this](const MousePressedEvent& event) {
            return onMousePressed(event);
        }, 100);

    // Subscribe to key press events for hotkeys
    keyPressHandlerId = EventDispatcher::subscribe<KeyPressedEvent>(
        [this](const KeyPressedEvent& event) {
            return onKeyPressed(event);
        }, 100);

}

void SceneDebugLayer::shutdown() {

    // Clear global pointer
    if (g_currentSceneDebugLayer == this) {
        g_currentSceneDebugLayer = nullptr;
    }

    if (mouseClickHandlerId != 0) {
        EventDispatcher::unsubscribe<MousePressedEvent>(mouseClickHandlerId);
        mouseClickHandlerId = 0;
    }

    if (keyPressHandlerId != 0) {
        EventDispatcher::unsubscribe<KeyPressedEvent>(keyPressHandlerId);
        keyPressHandlerId = 0;
    }

}

void SceneDebugLayer::onImGuiRender() {

    ImGui::Begin("Scene Debug");

    // Debug Rendering Controls
    if (renderer && ImGui::CollapsingHeader("Debug Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& debugRenderer = renderer->getDebugRenderer();

        // Note: Debug rendering state is now controlled only by user interaction

        bool debugEnabled = debugRenderer.isEnabled();
        if (ImGui::Checkbox("Enable Debug Rendering", &debugEnabled)) {
            debugRenderer.setEnabled(debugEnabled);
        }

        if (debugEnabled) {
            ImGui::Indent();

            bool showFrustum = debugRenderer.showFrustum();
            if (ImGui::Checkbox("Show Frustum", &showFrustum)) {
                debugRenderer.setShowFrustum(showFrustum);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "(Green)");

            bool showAABBs = debugRenderer.showAABBs();
            if (ImGui::Checkbox("Show AABB Bounds", &showAABBs)) {
                debugRenderer.setShowAABBs(showAABBs);
            }

            if (showAABBs) {
                ImGui::Indent();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Yellow: Visible");
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Red: Culled");
                ImGui::Unindent();
            }

            bool showRayDebug = showRay;
            if (ImGui::Checkbox("Show Mouse Ray", &showRayDebug)) {
                showRay = showRayDebug;
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "(Cyan)");

            if (showRay) {
                ImGui::Indent();

                // Ray management controls
                ImGui::Text("Stored Rays: %zu", storedRays.size());
                if (ImGui::Button("Clear All Rays")) {
                    clearAllRays();
                }

                // Show current ray management status
                ImGui::Text("Click in 3D scene to cast rays");
                ImGui::Text("Ray length calculated automatically from intersections");
                ImGui::Unindent();
            }

            ImGui::Unindent();
        }

        ImGui::Separator();
    }

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

    if (ImGui::CollapsingHeader("Scene Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (scene) {
            // Show hierarchical tree view of scene nodes
            ImGui::Text("Scene Nodes: %zu", scene->getNodeCount());
            ImGui::Separator();

            // Helper lambda to recursively render tree nodes
            auto renderNodeTree = [&](uint32_t nodeId, auto& self) -> void {
                const Node* node = scene->getNode(nodeId);
                if (!node) return;

                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (!node->hasChildren()) {
                    nodeFlags |= ImGuiTreeNodeFlags_Leaf;
                }
                if (node->entity == selectedEntity) {
                    nodeFlags |= ImGuiTreeNodeFlags_Selected;
                }

                eastl::string nodeLabel = node->name.empty() ? "Unnamed" : node->name;
                nodeLabel += " (ID: ";
                nodeLabel += std::to_string(nodeId).c_str();
                nodeLabel += ")";

                bool nodeOpen = ImGui::TreeNodeEx(nodeLabel.c_str(), nodeFlags);

                // Handle node selection
                if (ImGui::IsItemClicked() && node->entity != entt::null) {
                    selectedEntity = node->entity;
                    VT_INFO("Selected entity {} from hierarchy", static_cast<uint32_t>(selectedEntity));
                }

                // Show entity info if available
                if (node->entity != entt::null) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Entity: %u)", static_cast<uint32_t>(node->entity));

                    if (auto* transform = world->getRegistry().try_get<TransformComponent>(node->entity)) {
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("Local Position: (%.2f, %.2f, %.2f)",
                                transform->local.position.x, transform->local.position.y, transform->local.position.z);
                            ImGui::Text("World Position: (%.2f, %.2f, %.2f)",
                                transform->world.position.x, transform->world.position.y, transform->world.position.z);
                            ImGui::EndTooltip();
                        }
                    }
                }

                if (nodeOpen) {
                    for (uint32_t childId : node->childrenIds) {
                        self(childId, self);
                    }
                    ImGui::TreePop();
                }
            };

            // Render root nodes
            for (uint32_t rootId : scene->getRootNodes()) {
                renderNodeTree(rootId, renderNodeTree);
            }
        } else {
            ImGui::Text("No scene loaded - showing flat entity list");
            ImGui::Separator();

            // Fallback to flat entity table
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

                    // Highlight selected entity
                    if (entity == selectedEntity) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(100, 150, 200, 100));
                    }

                    eastl::string entityLabel = std::to_string(static_cast<uint32_t>(entity)).c_str();
                    if (ImGui::Selectable(entityLabel.c_str(),
                                         entity == selectedEntity, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedEntity = entity;
                        VT_INFO("Selected entity {} from table", static_cast<uint32_t>(selectedEntity));
                    }

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
    }

    if (ImGui::CollapsingHeader("Stats")) {
        // FPS Calculation
        static float lastTime = 0.0f;
        static float fps = 0.0f;
        static int frameCount = 0;

        float currentTime = ImGui::GetTime();
        frameCount++;

        if (currentTime - lastTime >= 1.0f) { // Update FPS every second
            fps = frameCount / (currentTime - lastTime);
            frameCount = 0;
            lastTime = currentTime;
        }

        ImGui::Text("FPS: %.1f", fps);
        ImGui::Separator();

        size_t totalEntities = world->getEntityCount();
        auto transformView = world->view<TransformComponent>();
        auto meshView = world->view<MeshComponent>();

        ImGui::Text("Total Entities: %zu", totalEntities);
        ImGui::Text("With Transform: %zu", transformView.size());
        ImGui::Text("With Mesh: %zu", meshView.size());

        // Rendering statistics
        if (renderer) {
            ImGui::Separator();
            ImGui::Text("Rendering Stats:");
            const auto& stats = renderer->getRenderStats();
            ImGui::Text("Total Renderables: %u", stats.totalRenderables);
            ImGui::Text("Visible Renderables: %u", stats.visibleRenderables);
            ImGui::Text("Draw Calls: %u", stats.drawCalls);
            ImGui::Text("Skipped: %u", stats.skippedRenderables);

            if (stats.totalRenderables > 0) {
                float cullingRate = (1.0f - (float)stats.visibleRenderables / (float)stats.totalRenderables) * 100.0f;
                ImGui::Text("Culling Rate: %.1f%%", cullingRate);
            }
        }
    }

    // Gizmo Controls
    if (ImGui::CollapsingHeader("Gizmo Controls")) {
        ImGui::Checkbox("Enable Gizmo", &enableGizmo);

        if (selectedEntity != entt::null) {
            ImGui::Text("Selected Entity: %u", static_cast<uint32_t>(selectedEntity));

            // Show entity information with editable fields
            auto& registry = world->getRegistry();
            if (auto* transform = registry.try_get<TransformComponent>(selectedEntity)) {
                ImGui::Text("Transform Editing:");

                // Position editing
                glm::vec3 localPos = transform->local.position;
                if (ImGui::DragFloat3("Position", &localPos.x, 0.1f, -1000.0f, 1000.0f, "%.2f")) {
                    transform->local.position = localPos;
                    transform->dirty = true;

                    // Update hierarchy
                    if (scene) {
                        scene->updateWorldTransforms(world->getRegistry());
                        // Update world bounds
                        auto meshView = world->view<TransformComponent, MeshComponent>();
                        for (auto [entity, transformComp, meshComp] : meshView.each()) {
                            if (transformComp.dirty) {
                                meshComp.updateWorldBounds(transformComp.world.getMatrix());
                                transformComp.dirty = false;
                            }
                        }
                    } else {
                        transform->world = transform->local;
                        transform->dirty = false;
                        if (auto* meshComp = registry.try_get<MeshComponent>(selectedEntity)) {
                            meshComp->updateWorldBounds(transform->world.getMatrix());
                        }
                    }
                }

                // Rotation editing (as Euler angles)
                glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(transform->local.rotation));
                if (ImGui::DragFloat3("Rotation", &eulerAngles.x, 1.0f, -180.0f, 180.0f, "%.1f°")) {
                    transform->local.rotation = glm::quat(glm::radians(eulerAngles));
                    transform->dirty = true;

                    // Update hierarchy
                    if (scene) {
                        scene->updateWorldTransforms(world->getRegistry());
                        auto meshView = world->view<TransformComponent, MeshComponent>();
                        for (auto [entity, transformComp, meshComp] : meshView.each()) {
                            if (transformComp.dirty) {
                                meshComp.updateWorldBounds(transformComp.world.getMatrix());
                                transformComp.dirty = false;
                            }
                        }
                    } else {
                        transform->world = transform->local;
                        transform->dirty = false;
                        if (auto* meshComp = registry.try_get<MeshComponent>(selectedEntity)) {
                            meshComp->updateWorldBounds(transform->world.getMatrix());
                        }
                    }
                }

                // Scale editing
                glm::vec3 localScale = transform->local.scale;
                if (ImGui::DragFloat3("Scale", &localScale.x, 0.01f, 0.001f, 100.0f, "%.3f")) {
                    transform->local.scale = localScale;
                    transform->dirty = true;

                    // Update hierarchy
                    if (scene) {
                        scene->updateWorldTransforms(world->getRegistry());
                        auto meshView = world->view<TransformComponent, MeshComponent>();
                        for (auto [entity, transformComp, meshComp] : meshView.each()) {
                            if (transformComp.dirty) {
                                meshComp.updateWorldBounds(transformComp.world.getMatrix());
                                transformComp.dirty = false;
                            }
                        }
                    } else {
                        transform->world = transform->local;
                        transform->dirty = false;
                        if (auto* meshComp = registry.try_get<MeshComponent>(selectedEntity)) {
                            meshComp->updateWorldBounds(transform->world.getMatrix());
                        }
                    }
                }

                ImGui::Separator();
                ImGui::Text("World Position: (%.2f, %.2f, %.2f)",
                    transform->world.position.x, transform->world.position.y, transform->world.position.z);
            }

            // Gizmo operation selection
            ImGui::RadioButton("Translate", &gizmoOperation, ImGuizmo::TRANSLATE);
            ImGui::SameLine();
            ImGui::RadioButton("Rotate", &gizmoOperation, ImGuizmo::ROTATE);
            ImGui::SameLine();
            ImGui::RadioButton("Scale", &gizmoOperation, ImGuizmo::SCALE);

            ImGui::Separator();

            // Coordinate system selection
            ImGui::Text("Coordinate System:");
            ImGui::RadioButton("World", &gizmoMode, ImGuizmo::WORLD);
            ImGui::SameLine();
            ImGui::RadioButton("Local", &gizmoMode, ImGuizmo::LOCAL);

            ImGui::Separator();

            // Snapping controls
            ImGui::Checkbox("Enable Snapping", &enableSnap);
            if (enableSnap) {
                ImGui::Indent();
                ImGui::SliderFloat("Translation", &snapTranslation, 0.1f, 5.0f, "%.1f units");
                ImGui::SliderFloat("Rotation", &snapRotation, 1.0f, 90.0f, "%.0f degrees");
                ImGui::SliderFloat("Scale", &snapScale, 0.01f, 1.0f, "%.2f");
                ImGui::Unindent();
            }

            if (ImGui::Button("Deselect")) {
                selectedEntity = entt::null;
                VT_INFO("Entity deselected via button");
            }
        } else {
            ImGui::Text("No entity selected");
            ImGui::Text("Left-click on objects in 3D scene to select");
        }

        ImGui::Text("Hotkeys: T(translate) R(rotate) E(scale) TAB(coord) CTRL(snap) ESC(deselect)");

        // Mouse status for debugging
        glm::vec2 mousePos = InputManager::getMousePosition();
        ImGui::Text("Mouse Position: (%.1f, %.1f)", mousePos.x, mousePos.y);
        ImGui::Text("ImGui WantCaptureMouse: %s", ImGui::GetIO().WantCaptureMouse ? "true" : "false");
        bool imGuizmoIsOver = (enableGizmo && selectedEntity != entt::null) ? ImGuizmo::IsOver() : false;
        ImGui::Text("ImGuizmo IsOver: %s", imGuizmoIsOver ? "true" : "false");
    }

    ImGui::End();

    // Render selected entity outline using debug renderer
    renderSelectedEntityOutline();

    // Render ray visualization
    renderRayVisualization();

    // Render Gizmo (must be after ImGui::End())
    renderGizmo();
}

bool SceneDebugLayer::onMousePressed(const MousePressedEvent& event) {
    if (!renderer) return false;


    // Only handle left mouse button
    if (event.button != MouseButton::Left) {
        return false;  // Don't consume, let others handle
    }

    // Check if ImGui or ImGuizmo wants to capture the mouse
    bool wantCapture = ImGui::GetIO().WantCaptureMouse;
    bool isOverGizmo = false;

    // Only check ImGuizmo::IsOver if gizmo is enabled and entity is selected
    if (enableGizmo && selectedEntity != entt::null) {
        isOverGizmo = ImGuizmo::IsOver();
    }


    if (wantCapture || isOverGizmo) {
        return false;  // Don't consume, let ImGui/ImGuizmo handle
    }

    // Perform object picking
    VT_INFO("Attempting object pick at mouse position: ({}, {})", event.position.x, event.position.y);

    entt::entity picked = pickObject(event.position.x, event.position.y);
    if (picked != entt::null) {
        selectedEntity = picked;
        VT_INFO("Selected entity: {}", static_cast<uint32_t>(selectedEntity));
    } else {
        selectedEntity = entt::null;
        VT_DEBUG("No entity picked");
    }

    // Ray generation for debugging - do this regardless of pick success
    if (showRay) {
        addRayFromMouseClick(event.position.x, event.position.y);
    }

    return true;  // Consume the event since we handled object selection
}

bool SceneDebugLayer::onKeyPressed(const KeyPressedEvent& event) {
    VT_DEBUG("SceneDebugLayer::onKeyPressed() called - key: {}", event.key);

    // Only handle gizmo hotkeys when gizmo is enabled and entity is selected
    bool shouldHandleGizmoKeys = enableGizmo && selectedEntity != entt::null;

    // Handle gizmo hotkeys
    switch (event.key) {
        case GLFW_KEY_T:
            if (shouldHandleGizmoKeys) {
                gizmoOperation = ImGuizmo::TRANSLATE;
                VT_INFO("Gizmo operation changed to TRANSLATE");
                return true;
            }
            break;
        case GLFW_KEY_R:
            if (shouldHandleGizmoKeys) {
                gizmoOperation = ImGuizmo::ROTATE;
                VT_INFO("Gizmo operation changed to ROTATE");
                return true;
            }
            break;
        case GLFW_KEY_E:
            if (shouldHandleGizmoKeys) {
                gizmoOperation = ImGuizmo::SCALE;
                VT_INFO("Gizmo operation changed to SCALE");
                return true;
            }
            break;
        case GLFW_KEY_ESCAPE:
            selectedEntity = entt::null;
            VT_INFO("Entity deselected");
            return true;
        case GLFW_KEY_TAB:
            gizmoMode = (gizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            VT_INFO("Gizmo coordinate system changed to {}", (gizmoMode == ImGuizmo::WORLD) ? "WORLD" : "LOCAL");
            return true;
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL:
            enableSnap = !enableSnap;
            VT_INFO("Gizmo snapping {}", enableSnap ? "enabled" : "disabled");
            return true;
        default:
            return false;  // Don't consume other keys
    }

    return false;  // Default: don't consume the event
}


entt::entity SceneDebugLayer::pickObject(float mouseX, float mouseY) {
    if (!renderer || !world) {
        VT_WARN("Renderer or world is null in pickObject");
        return entt::null;
    }

    // Find active camera
    auto cameraView = world->view<CameraComponent>();
    Camera* activeCamera = nullptr;
    for (auto entity : cameraView) {
        auto& cameraComp = cameraView.get<CameraComponent>(entity);
        if (cameraComp.isActive && cameraComp.camera) {
            activeCamera = cameraComp.camera.get();
            break;
        }
    }

    if (!activeCamera) {
        VT_WARN("No active camera found for picking");
        return entt::null;
    }

    // Create ray from mouse position
    const ImGuiIO& io = ImGui::GetIO();
    float x = (2.0f * mouseX) / io.DisplaySize.x - 1.0f;
    float y = (2.0f * mouseY) / io.DisplaySize.y - 1.0f;  // Correct Y for Vulkan (no double flip)


    glm::mat4 view = activeCamera->getViewMatrix();
    glm::mat4 proj = activeCamera->getProjectionMatrix();
    glm::mat4 invViewProj = glm::inverse(proj * view);

    // Vulkan NDC: Z ranges from 0 (near) to 1 (far)
    // Generate ray from actual camera position through the clicked point
    glm::vec4 rayNear_NDC(x, y, 0.0f, 1.0f);  // Near plane (z=0 in Vulkan)
    glm::vec4 rayFar_NDC(x, y, 1.0f, 1.0f);    // Far plane (z=1 in Vulkan)

    glm::vec4 rayNear_world = invViewProj * rayNear_NDC;
    glm::vec4 rayFar_world = invViewProj * rayFar_NDC;

    // Perspective divide
    if (rayNear_world.w != 0.0f) rayNear_world /= rayNear_world.w;
    if (rayFar_world.w != 0.0f) rayFar_world /= rayFar_world.w;

    // Ray originates from the actual camera position
    glm::vec3 rayOrigin = activeCamera->getPosition();
    // Direction from camera to the unprojected point
    glm::vec3 targetPoint(rayNear_world);  // Use near plane point as target
    glm::vec3 rayDirection = glm::normalize(targetPoint - rayOrigin);


    Ray ray(rayOrigin, rayDirection);

    // Find closest intersected entity
    float closestDistance = FLT_MAX;
    entt::entity closestEntity = entt::null;
    int entitiesChecked = 0;
    int intersectionsFound = 0;

    auto& registry = world->getRegistry();
    auto entityView = registry.view<TransformComponent, MeshComponent>();
    for (auto entity : entityView) {
        auto& meshComp = entityView.get<MeshComponent>(entity);
        entitiesChecked++;

        if (meshComp.mesh) {
            // Test intersection with each submesh AABB
            size_t subMeshCount = meshComp.getSubMeshCount();

            for (size_t i = 0; i < subMeshCount; ++i) {
                const AABB& bounds = meshComp.getSubMeshWorldBounds(i);

                if (ray.intersectAABB(bounds)) {
                    intersectionsFound++;
                    // Calculate distance to AABB center
                    glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
                    float distance = glm::length(rayOrigin - center);


                    if (distance < closestDistance) {
                        closestDistance = distance;
                        closestEntity = entity;
                    }
                }
            }
        }
    }


    return closestEntity;
}

void SceneDebugLayer::renderGizmo() {
    if (!enableGizmo || selectedEntity == entt::null || !renderer || !world) {
        return;
    }

    // Get selected entity's transform
    auto& registry = world->getRegistry();
    auto* transform = registry.try_get<TransformComponent>(selectedEntity);
    if (!transform) {
        VT_WARN("Selected entity {} has no TransformComponent", static_cast<uint32_t>(selectedEntity));
        return;
    }

    // Find active camera
    auto cameraView = world->view<CameraComponent>();
    Camera* activeCamera = nullptr;
    for (auto entity : cameraView) {
        auto& cameraComp = cameraView.get<CameraComponent>(entity);
        if (cameraComp.isActive && cameraComp.camera) {
            activeCamera = cameraComp.camera.get();
            break;
        }
    }

    if (!activeCamera) {
        VT_WARN("No active camera found for gizmo rendering");
        return;
    }

    // Create transparent overlay window for gizmo
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);

    // Window flags for transparent overlay
    ImGuiWindowFlags gizmoWindowFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoFocusOnAppearing;

    // Check if we should allow mouse input (preview check)
    // We need to call ImGuizmo functions after SetDrawlist, so we'll do a conditional setup
    static bool wasOverGizmo = false;

    // Only allow mouse input if we were over gizmo in the previous frame
    // This prevents the chicken-and-egg problem with ImGuizmo::IsOver()
    if (!wasOverGizmo) {
        gizmoWindowFlags |= ImGuiWindowFlags_NoMouseInputs;
    }

    bool gizmoWindowOpen = true;
    if (!ImGui::Begin("##GizmoOverlay", &gizmoWindowOpen, gizmoWindowFlags)) {
        ImGui::End();
        return;
    }

    // Set up ImGuizmo for this window
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    glm::mat4 view = activeCamera->getViewMatrix();
    glm::mat4 proj = activeCamera->getProjectionMatrix();

    // Fix Vulkan coordinate system for ImGuizmo
    // ImGuizmo expects standard projection matrix, so undo Vulkan Y-flip
    glm::mat4 gizmoProj = proj;
    gizmoProj[1][1] *= -1.0f;  // Undo the Vulkan Y-axis flip

    // Get current transform matrix
    glm::mat4 matrix = transform->world.getMatrix();

    // Log gizmo rendering attempt
    VT_DEBUG("Rendering gizmo for entity {} at position ({:.2f}, {:.2f}, {:.2f})",
        static_cast<uint32_t>(selectedEntity),
        transform->world.position.x, transform->world.position.y, transform->world.position.z);
    VT_DEBUG("Gizmo operation: {}, mode: {}", gizmoOperation, gizmoMode);

    // Prepare snap values if snapping is enabled
    float* snap = nullptr;
    float snapValues[3] = {0.0f, 0.0f, 0.0f};
    if (enableSnap) {
        if (gizmoOperation == ImGuizmo::TRANSLATE) {
            snapValues[0] = snapValues[1] = snapValues[2] = snapTranslation;
        } else if (gizmoOperation == ImGuizmo::ROTATE) {
            snapValues[0] = snapValues[1] = snapValues[2] = snapRotation;
        } else if (gizmoOperation == ImGuizmo::SCALE) {
            snapValues[0] = snapValues[1] = snapValues[2] = snapScale;
        }
        snap = snapValues;
    }

    // Manipulate transform using corrected projection matrix
    bool gizmoUsed = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(gizmoProj),
        static_cast<ImGuizmo::OPERATION>(gizmoOperation),
        static_cast<ImGuizmo::MODE>(gizmoMode),
        glm::value_ptr(matrix),
        nullptr, // deltaMatrix (not needed)
        snap); // snap values

    // Update gizmo state for next frame's mouse input decision
    bool currentlyOverGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    if (currentlyOverGizmo != wasOverGizmo) {
        VT_DEBUG("Gizmo mouse state changed - IsOver: {}, IsUsing: {}, WillCaptureMouse: {}",
                 ImGuizmo::IsOver(), ImGuizmo::IsUsing(), currentlyOverGizmo);
        wasOverGizmo = currentlyOverGizmo;
    }

    if (gizmoUsed) {
        VT_DEBUG("Gizmo was manipulated for entity {}", static_cast<uint32_t>(selectedEntity));

        // Decompose matrix back to transform components
        glm::vec3 translation, rotation, scale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(matrix),
            glm::value_ptr(translation),
            glm::value_ptr(rotation),
            glm::value_ptr(scale));

        // Update local transform
        transform->local.position = translation;
        transform->local.rotation = glm::quat(glm::radians(rotation));
        transform->local.scale = scale;
        transform->dirty = true;  // Mark for update

        // Update world transforms through hierarchy if scene is available
        if (scene) {
            scene->updateWorldTransforms(world->getRegistry());

            // Update world bounds for all affected entities
            auto meshView = world->view<TransformComponent, MeshComponent>();
            for (auto [entity, transformComp, meshComp] : meshView.each()) {
                if (transformComp.dirty) {
                    meshComp.updateWorldBounds(transformComp.world.getMatrix());
                    transformComp.dirty = false;
                }
            }
        } else {
            // Fallback to simple local == world if no scene hierarchy
            transform->world = transform->local;
            transform->dirty = false;

            // Update bounds for this entity only
            if (auto* meshComp = world->getRegistry().try_get<MeshComponent>(selectedEntity)) {
                meshComp->updateWorldBounds(transform->world.getMatrix());
            }
        }

        // Trigger BVH rebuild for spatial acceleration structures
        // Mark the scene as dirty so the renderer rebuilds the BVH
        if (renderer) {
            renderer->markSceneDirty();
            VT_DEBUG("Transform modified by gizmo - marked scene dirty for BVH rebuild");
        }
    } else {
        // Check if gizmo is visible/active
        static int frameCounter = 0;
        if (++frameCounter % 60 == 0) {  // Log every 60 frames to avoid spam
            VT_DEBUG("Gizmo not being manipulated - IsUsing: {}, IsOver: {}",
                ImGuizmo::IsUsing(), ImGuizmo::IsOver());
        }
    }

    // Close the transparent overlay window
    ImGui::End();
}

void SceneDebugLayer::renderSelectedEntityOutline() {
    if (selectedEntity != entt::null && renderer) {
        auto& debugRenderer = renderer->getDebugRenderer();
        debugRenderer.setSelectedEntity(selectedEntity);
        // Note: Actual rendering will happen in Renderer's renderSelectedEntity call
    }
}

void SceneDebugLayer::renderRayVisualization() {
    if (!renderer || !showRay) {
        return;
    }

    try {
        auto& debugRenderer = renderer->getDebugRenderer();

        // Clear old ray data
        debugRenderer.clearRayData();

        // Store rays in the renderer for access during rendering
        if (!storedRays.empty()) {
            // For now, just enable ray rendering with the first valid ray
            // The renderer will call back to get all rays during render
            const StoredRay& firstRay = storedRays[0];
            if (std::isfinite(firstRay.origin.x) && std::isfinite(firstRay.origin.y) && std::isfinite(firstRay.origin.z) &&
                std::isfinite(firstRay.direction.x) && std::isfinite(firstRay.direction.y) && std::isfinite(firstRay.direction.z) &&
                std::isfinite(firstRay.length) && firstRay.length > 0.0f) {
                debugRenderer.setRayData(firstRay.origin, firstRay.direction, firstRay.length, true);
            }
        }
    } catch (const std::exception& e) {
        VT_ERROR("Exception in renderRayVisualization: {}", e.what());
    } catch (...) {
        VT_ERROR("Unknown exception in renderRayVisualization");
    }
}

void SceneDebugLayer::addRayFromMouseClick(float mouseX, float mouseY) {
    if (!renderer || !world) {
        return;
    }

    // Find active camera
    auto cameraView = world->view<CameraComponent>();
    Camera* activeCamera = nullptr;
    for (auto entity : cameraView) {
        auto& cameraComp = cameraView.get<CameraComponent>(entity);
        if (cameraComp.isActive && cameraComp.camera) {
            activeCamera = cameraComp.camera.get();
            break;
        }
    }

    if (!activeCamera) {
        return;
    }

    // Create ray from mouse position
    const ImGuiIO& io = ImGui::GetIO();
    float x = (2.0f * mouseX) / io.DisplaySize.x - 1.0f;
    float y = (2.0f * mouseY) / io.DisplaySize.y - 1.0f;  // Correct Y for Vulkan (no double flip)

    glm::mat4 view = activeCamera->getViewMatrix();
    glm::mat4 proj = activeCamera->getProjectionMatrix();
    glm::mat4 invViewProj = glm::inverse(proj * view);

    // Vulkan NDC: Z ranges from 0 (near) to 1 (far)
    // Generate ray from actual camera position through the clicked point
    glm::vec4 rayNear_NDC(x, y, 0.0f, 1.0f);  // Near plane (z=0 in Vulkan)
    glm::vec4 rayFar_NDC(x, y, 1.0f, 1.0f);    // Far plane (z=1 in Vulkan)

    glm::vec4 rayNear_world = invViewProj * rayNear_NDC;
    glm::vec4 rayFar_world = invViewProj * rayFar_NDC;

    // Perspective divide
    if (rayNear_world.w != 0.0f) rayNear_world /= rayNear_world.w;
    if (rayFar_world.w != 0.0f) rayFar_world /= rayFar_world.w;

    // Ray originates from the actual camera position
    glm::vec3 rayOrigin = activeCamera->getPosition();

    // Create proper ray direction using both near and far plane points
    glm::vec3 nearPoint(rayNear_world);
    glm::vec3 farPoint(rayFar_world);

    // Ray direction is from near to far plane (through the mouse click point)
    glm::vec3 rayDirection = glm::normalize(farPoint - nearPoint);

    // Calculate ray length based on scene intersections
    Ray ray(rayOrigin, rayDirection);
    float calculatedRayLength = 1000.0f;  // Default long distance

    // Find closest intersection with scene geometry
    float closestDistance = FLT_MAX;
    int intersectionCount = 0;
    auto& registry = world->getRegistry();
    auto entityView = registry.view<TransformComponent, MeshComponent>();

    for (auto entity : entityView) {
        auto& meshComp = entityView.get<MeshComponent>(entity);
        if (meshComp.mesh) {
            size_t subMeshCount = meshComp.getSubMeshCount();
            for (size_t i = 0; i < subMeshCount; ++i) {
                const AABB& bounds = meshComp.getSubMeshWorldBounds(i);

                float tNear, tFar;
                if (ray.intersectAABB(bounds, tNear, tFar)) {
                    // Use the entry point (tNear) if it's in front of the camera
                    // Otherwise use the exit point (tFar) if the ray starts inside
                    float hitDistance = (tNear > 0.001f) ? tNear : tFar;

                    if (hitDistance > 0.001f && hitDistance < closestDistance) {
                        closestDistance = hitDistance;
                        intersectionCount++;
                    }
                }
            }
        }
    }


    // Use intersection distance if found, otherwise use default
    if (closestDistance < FLT_MAX) {
        calculatedRayLength = closestDistance;
    }

    // Validate ray data before storing
    if (!std::isfinite(rayOrigin.x) || !std::isfinite(rayOrigin.y) || !std::isfinite(rayOrigin.z) ||
        !std::isfinite(rayDirection.x) || !std::isfinite(rayDirection.y) || !std::isfinite(rayDirection.z) ||
        !std::isfinite(calculatedRayLength) || calculatedRayLength <= 0.0f) {
        VT_WARN("Invalid ray data generated from mouse click at ({}, {}), skipping storage", mouseX, mouseY);
        return;
    }

    // Store the ray as fixed in world space
    StoredRay newRay;
    newRay.origin = rayOrigin;
    newRay.direction = rayDirection;
    newRay.length = calculatedRayLength;

    storedRays.push_back(newRay);

}

void SceneDebugLayer::clearAllRays() {
    storedRays.clear();
    if (renderer) {
        renderer->getDebugRenderer().clearRayData();
    }
}

}