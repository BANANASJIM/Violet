#include "core/MathUtils.hpp"
#include "core/Exception.hpp"
#include "SceneDebugLayer.hpp"
#include "input/InputManager.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include "renderer/ForwardRenderer.hpp"
#include "ecs/Components.hpp"
#include "math/Ray.hpp"
#include "scene/Scene.hpp"
#include "scene/Node.hpp"
#include "core/FileSystem.hpp"
#include <EASTL/sort.h>

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

    // First render the main Scene Debug window
    // This ensures proper Z-ordering

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
        renderSceneHierarchy();
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
                        for (auto&& [entity, transformComp, meshComp] : meshView.each()) {
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
                        for (auto&& [entity, transformComp, meshComp] : meshView.each()) {
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
                        for (auto&& [entity, transformComp, meshComp] : meshView.each()) {
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

    // Now handle drag-drop overlay AFTER the main window
    // This ensures it's rendered on top
    handleAssetDragDrop();

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

    entt::entity picked = pickObject(event.position.x, event.position.y);
    if (picked != entt::null) {
        selectedEntity = picked;
    } else {
        selectedEntity = entt::null;
    }

    // Ray generation for debugging - do this regardless of pick success
    if (showRay) {
        addRayFromMouseClick(event.position.x, event.position.y);
    }

    return true;  // Consume the event since we handled object selection
}

bool SceneDebugLayer::onKeyPressed(const KeyPressedEvent& event) {

    // Only handle gizmo hotkeys when gizmo is enabled and entity is selected
    bool shouldHandleGizmoKeys = enableGizmo && selectedEntity != entt::null;

    // Handle gizmo hotkeys
    switch (event.key) {
        case GLFW_KEY_T:
            if (shouldHandleGizmoKeys) {
                gizmoOperation = ImGuizmo::TRANSLATE;
                return true;
            }
            break;
        case GLFW_KEY_R:
            if (shouldHandleGizmoKeys) {
                gizmoOperation = ImGuizmo::ROTATE;
                return true;
            }
            break;
        case GLFW_KEY_E:
            if (shouldHandleGizmoKeys) {
                gizmoOperation = ImGuizmo::SCALE;
                return true;
            }
            break;
        case GLFW_KEY_ESCAPE:
            selectedEntity = entt::null;
            return true;
        case GLFW_KEY_TAB:
            gizmoMode = (gizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            return true;
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL:
            enableSnap = !enableSnap;
            return true;
        default:
            return false;  // Don't consume other keys
    }

    return false;  // Default: don't consume the event
}


entt::entity SceneDebugLayer::pickObject(float mouseX, float mouseY) {
    if (!renderer || !world) {
        violet::Log::warn("UI", "Renderer or world is null in pickObject");
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
        violet::Log::warn("UI", "No active camera found for picking");
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
        violet::Log::warn("UI", "Selected entity {} has no TransformComponent", static_cast<uint32_t>(selectedEntity));
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
        violet::Log::warn("UI", "No active camera found for gizmo rendering");
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

    // Allow mouse input if we were over gizmo in the previous frame
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
    glm::mat4 gizmoProj = proj;
    gizmoProj[1][1] *= -1.0f;  // Undo the Vulkan Y-axis flip

    glm::mat4 matrix = transform->world.getMatrix();

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

    // Don't restrict gizmo operations - let all entities use T/R/E normally
    ImGuizmo::OPERATION currentGizmoOperation = static_cast<ImGuizmo::OPERATION>(gizmoOperation);

    // Manipulate transform using corrected projection matrix
    bool gizmoUsed = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(gizmoProj),
        currentGizmoOperation,
        static_cast<ImGuizmo::MODE>(gizmoMode),
        glm::value_ptr(matrix),
        nullptr, // deltaMatrix (not needed)
        snap); // snap values

    // Update gizmo state for next frame's mouse input decision
    bool currentlyOverGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    if (currentlyOverGizmo != wasOverGizmo) {
        wasOverGizmo = currentlyOverGizmo;
    }

    if (gizmoUsed) {
        // Find the node ID for the selected entity
        uint32_t nodeId = 0;
        if (scene) {
            nodeId = scene->findNodeIdForEntity(selectedEntity);
        }

        glm::mat4 finalMatrix = matrix;

        // Check if this is a child node and convert coordinates if needed
        if (scene && nodeId != 0 && !scene->isRootNode(nodeId)) {
            // For child nodes: convert world transform to local transform
            finalMatrix = scene->convertWorldToLocal(nodeId, matrix, world->getRegistry());
        }

        // Decompose the final matrix back to transform components
        glm::vec3 translation, rotation, scale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(finalMatrix),
            glm::value_ptr(translation),
            glm::value_ptr(rotation),
            glm::value_ptr(scale));

        // Update local transform
        transform->local.position = translation;
        transform->local.rotation = glm::quat(glm::radians(rotation));
        transform->local.scale = scale;
        transform->dirty = true;  // Mark for update

        // Special handling for directional lights: only rotation affects direction
        auto* light = registry.try_get<LightComponent>(selectedEntity);
        if (light && light->type == LightType::Directional &&
            currentGizmoOperation == ImGuizmo::ROTATE) {
            // Extract direction from the rotated matrix
            // Default direction is (0, -1, 0), apply rotation to get new direction
            glm::mat3 rotationMatrix = glm::mat3(finalMatrix);
            light->direction = glm::normalize(rotationMatrix * glm::vec3(0.0f, -1.0f, 0.0f));
        }

        // Special handling for point lights: scale controls radius
        if (light && light->type == LightType::Point &&
            currentGizmoOperation == ImGuizmo::SCALE) {
            // Use uniform scale (average of xyz) as radius multiplier
            float scaleMultiplier = (scale.x + scale.y + scale.z) / 3.0f;
            light->radius *= scaleMultiplier;
            // Clamp radius to reasonable range
            light->radius = glm::clamp(light->radius, 1.0f, 10000.0f);
            // Reset scale to (1,1,1) to prevent visual distortion
            transform->local.scale = glm::vec3(1.0f);
        }

        // Update world transforms through hierarchy if scene is available
        if (scene) {
            scene->updateWorldTransforms(world->getRegistry());

            // Update world bounds for all affected entities
            auto meshView = world->view<TransformComponent, MeshComponent>();
            for (auto&& [entity, transformComp, meshComp] : meshView.each()) {
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
            if (violet::isfinite(firstRay.origin.x) && violet::isfinite(firstRay.origin.y) && violet::isfinite(firstRay.origin.z) &&
                violet::isfinite(firstRay.direction.x) && violet::isfinite(firstRay.direction.y) && violet::isfinite(firstRay.direction.z) &&
                violet::isfinite(firstRay.length) && firstRay.length > 0.0f) {
                debugRenderer.setRayData(firstRay.origin, firstRay.direction, firstRay.length, true);
            }
        }
    } catch (const violet::Exception& e) {
        violet::Log::error("UI", "Exception in renderRayVisualization: {}", e.what_c_str());
    } catch (...) {
        violet::Log::error("UI", "Unknown exception in renderRayVisualization");
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
    if (!violet::isfinite(rayOrigin.x) || !violet::isfinite(rayOrigin.y) || !violet::isfinite(rayOrigin.z) ||
        !violet::isfinite(rayDirection.x) || !violet::isfinite(rayDirection.y) || !violet::isfinite(rayDirection.z) ||
        !violet::isfinite(calculatedRayLength) || calculatedRayLength <= 0.0f) {
        violet::Log::warn("UI", "Invalid ray data generated from mouse click at ({}, {}), skipping storage", mouseX, mouseY);
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

glm::vec3 SceneDebugLayer::calculatePlacementPosition(float mouseX, float mouseY) {
    if (!renderer || !world) {
        return glm::vec3(0.0f, 0.0f, 0.0f);
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
        return glm::vec3(0.0f, 0.0f, 0.0f);
    }

    // Create ray from mouse position (reuse logic from addRayFromMouseClick)
    const ImGuiIO& io = ImGui::GetIO();
    float x = (2.0f * mouseX) / io.DisplaySize.x - 1.0f;
    float y = (2.0f * mouseY) / io.DisplaySize.y - 1.0f;

    glm::mat4 view = activeCamera->getViewMatrix();
    glm::mat4 proj = activeCamera->getProjectionMatrix();
    glm::mat4 invViewProj = glm::inverse(proj * view);

    // Generate ray from camera through mouse position
    glm::vec4 rayNear_NDC(x, y, 0.0f, 1.0f);  // Near plane
    glm::vec4 rayFar_NDC(x, y, 1.0f, 1.0f);    // Far plane

    glm::vec4 rayNear_world = invViewProj * rayNear_NDC;
    glm::vec4 rayFar_world = invViewProj * rayFar_NDC;

    // Perspective divide
    if (rayNear_world.w != 0.0f) rayNear_world /= rayNear_world.w;
    if (rayFar_world.w != 0.0f) rayFar_world /= rayFar_world.w;

    // Use same ray calculation as pickObject
    glm::vec3 rayOrigin = activeCamera->getPosition();
    glm::vec3 targetPoint(rayNear_world);  // Use near plane point as target
    glm::vec3 rayDirection = glm::normalize(targetPoint - rayOrigin);

    Ray ray(rayOrigin, rayDirection);
    float closestDistance = FLT_MAX;
    bool foundIntersection = false;

    auto& registry = world->getRegistry();
    auto entityView = registry.view<TransformComponent, MeshComponent>();

    for (auto entity : entityView) {
        auto& meshComp = entityView.get<MeshComponent>(entity);
        if (!meshComp.mesh) continue;

        // Test intersection with each submesh AABB like pickObject
        size_t subMeshCount = meshComp.getSubMeshCount();
        for (size_t i = 0; i < subMeshCount; ++i) {
            const AABB& bounds = meshComp.getSubMeshWorldBounds(i);

            float tNear, tFar;
            if (ray.intersectAABB(bounds, tNear, tFar)) {
                float hitDistance = (tNear > 0.001f) ? tNear : tFar;
                if (hitDistance > 0.001f && hitDistance < closestDistance) {
                    closestDistance = hitDistance;
                    foundIntersection = true;
                }
            }
        }
    }

    if (foundIntersection) {
        // Place at intersection point
        return rayOrigin + rayDirection * closestDistance;
    } else {
        // No intersection found, place at origin
        return glm::vec3(0.0f, 0.0f, 0.0f);
    }
}

void SceneDebugLayer::handleAssetDragDrop() {
    ImGuiIO& io = ImGui::GetIO();

    // Create full-screen invisible window for drop target
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f); // Fully transparent

    // Check if currently dragging
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    bool isDragging = payload && eastl::string(payload->DataType) == "ASSET_PATH";

    // Log drag state for debugging
    static bool wasDragging = false;
    if (isDragging && !wasDragging) {
        violet::Log::info("UI", "Started dragging asset");
        wasDragging = true;
    } else if (!isDragging && wasDragging) {
        violet::Log::info("UI", "Stopped dragging asset");
        wasDragging = false;
    }

    // Window flags - always allow mouse input to receive drops
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse |
                            ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoFocusOnAppearing |
                            ImGuiWindowFlags_NoBringToFrontOnFocus |
                            ImGuiWindowFlags_NoBackground;

    // Don't block mouse input when dragging
    if (!isDragging) {
        flags |= ImGuiWindowFlags_NoMouseInputs;
    }

    if (ImGui::Begin("##SceneDropTarget", nullptr, flags)) {
        // Make entire window a drop target
        ImGui::InvisibleButton("##DropZone", io.DisplaySize);

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* dropPayload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                const char* path = (const char*)dropPayload->Data;
                violet::Log::info("UI", "Asset dropped in scene: {}", path);

                float mouseX = io.MousePos.x;
                float mouseY = io.MousePos.y;

                if (onAssetDroppedWithPosition) {
                    glm::vec3 placementPos = calculatePlacementPosition(mouseX, mouseY);
                    onAssetDroppedWithPosition(path, placementPos);
                } else if (onAssetDropped) {
                    onAssetDropped(path);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::End();

    // Render Light Control Window
    renderLightControlWindow();
}

void SceneDebugLayer::renderLightControlWindow() {
    ImGui::Begin("Light Control");

    auto& registry = world->getRegistry();

    // Add new lights section
    if (ImGui::CollapsingHeader("Add New Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Add Directional Light")) {
            createLightEntity(LightType::Directional);
            violet::Log::info("UI", "Created new directional light");
        }

        ImGui::SameLine();

        if (ImGui::Button("Add Point Light")) {
            // Place point light near camera if possible
            Camera* activeCamera = findActiveCamera();
            glm::vec3 position = glm::vec3(0.0f, 100.0f, 0.0f);
            if (activeCamera) {
                position = activeCamera->getPosition() + activeCamera->getForward() * 200.0f;
            }
            createLightEntity(LightType::Point, position);
            violet::Log::info("UI", "Created new point light");
        }
    }

    ImGui::Separator();

    // Light list section
    if (ImGui::CollapsingHeader("Light List", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto lightView = registry.view<LightComponent, TransformComponent>();

        // Count lights
        size_t lightCount = 0;
        for (auto entity : lightView) {
            lightCount++;
        }
        ImGui::Text("Total Lights: %zu", lightCount);
        ImGui::Separator();

        // List all lights using helper function
        int lightIndex = 0;
        for (auto entity : lightView) {
            auto& light = lightView.get<LightComponent>(entity);
            renderLightListItem(entity, light, lightIndex++);
        }
    }

    ImGui::Separator();

    // Light properties editor
    if (selectedEntity != entt::null) {
        if (auto* light = registry.try_get<LightComponent>(selectedEntity)) {
            if (ImGui::CollapsingHeader("Light Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
                renderLightProperties(selectedEntity, light);
            }
        } else {
            // Selected entity is not a light - show info but don't reset selection
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Selected entity is not a light");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Select a light from the list above to edit properties");
        }
    } else {
        ImGui::Text("No light selected");
        ImGui::Text("Select a light from the list above");
    }

    ImGui::Separator();

    // Environment panel
    renderEnvironmentPanel();

    ImGui::End();
}

// Helper function implementations
Camera* SceneDebugLayer::findActiveCamera() {
    if (!world) return nullptr;

    auto cameraView = world->view<CameraComponent>();
    for (auto entity : cameraView) {
        auto& cameraComp = cameraView.get<CameraComponent>(entity);
        if (cameraComp.isActive && cameraComp.camera) {
            return cameraComp.camera.get();
        }
    }
    return nullptr;
}

TransformComponent SceneDebugLayer::createInitializedTransform(const glm::vec3& position) {
    TransformComponent transform;
    transform.local.position = position;
    transform.world = transform.local;
    transform.dirty = false;
    return transform;
}

entt::entity SceneDebugLayer::createLightEntity(LightType type, const glm::vec3& position) {
    if (!world) return entt::null;

    auto& registry = world->getRegistry();
    auto entity = registry.create();

    // Add transform component
    registry.emplace<TransformComponent>(entity, createInitializedTransform(position));

    // Add light component based on type
    eastl::string lightName;
    if (type == LightType::Directional) {
        auto light = LightComponent::createDirectionalLight(
            glm::vec3(-0.3f, -1.0f, -0.3f),
            glm::vec3(1.0f, 1.0f, 1.0f),
            30.0f  // 30,000 lux (bright daylight)
        );
        registry.emplace<LightComponent>(entity, light);
        lightName = "Directional Light";
    } else {
        auto light = LightComponent::createPointLight(
            glm::vec3(1.0f, 1.0f, 1.0f),
            800.0f,   // 800 lumens (60W bulb equivalent)
            300.0f    // 300 unit radius
        );
        registry.emplace<LightComponent>(entity, light);
        lightName = "Point Light";
    }

    // Add light to scene hierarchy if scene exists
    if (scene) {
        // Create a scene node for the light
        Node lightNode;
        lightNode.name = lightName;
        lightNode.entity = entity;
        lightNode.parentId = 0;  // Make it a root node

        // Add node to scene (will automatically be added as root since parentId = 0)
        uint32_t nodeId = scene->addNode(lightNode);

        // Update world transforms to ensure the light's position is properly set
        scene->updateWorldTransforms(world->getRegistry());

        violet::Log::info("UI", "Added light entity {} to scene hierarchy as node {}", static_cast<uint32_t>(entity), nodeId);
    }

    selectedEntity = entity;
    return entity;
}

const char* SceneDebugLayer::getLightTypeIcon(LightType type) {
    return (type == LightType::Directional) ? "[DIR]" : "[POINT]";
}

const char* SceneDebugLayer::getLightTypeString(LightType type) {
    return (type == LightType::Directional) ? "Directional" : "Point";
}

void SceneDebugLayer::renderLightListItem(entt::entity entity, const LightComponent& light, int index) {
    const char* typeIcon = getLightTypeIcon(light.type);
    const char* enabledIcon = light.enabled ? "ON" : "OFF";
    bool isSelected = (entity == selectedEntity);

    ImGui::PushID(index);

    // Make the entire row selectable
    char label[256];
    snprintf(label, sizeof(label), "%s Light %u [%s]", typeIcon, static_cast<uint32_t>(entity), enabledIcon);

    if (ImGui::Selectable(label, isSelected)) {
        selectedEntity = entity;
    }

    // Quick toggle enable/disable
    ImGui::SameLine(ImGui::GetWindowWidth() - 60);
    ImGui::PushStyleColor(ImGuiCol_Button, light.enabled ? ImVec4(0.2f, 0.7f, 0.2f, 1.0f) : ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    if (ImGui::SmallButton(light.enabled ? "ON" : "OFF")) {
        // Note: This modifies a const reference, but it's safe in this context
        const_cast<LightComponent&>(light).enabled = !light.enabled;
    }
    ImGui::PopStyleColor();

    ImGui::PopID();
}

// Removed: Attenuation presets no longer needed with physical light units

void SceneDebugLayer::renderLightProperties(entt::entity entity, LightComponent* light) {
    if (!world || !light) return;

    auto& registry = world->getRegistry();

    ImGui::Text("Entity ID: %u", static_cast<uint32_t>(entity));
    ImGui::Text("Type: %s", getLightTypeString(light->type));

    // User guidance
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "💡 Controls:");
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
        "  • Click to select light  • T/R/E keys for gizmo  • Adjust properties below");

    ImGui::Separator();

    // Common properties
    ImGui::ColorEdit3("Color", &light->color.x);

    // Type-specific intensity with physical units
    if (light->type == LightType::Directional) {
        ImGui::DragFloat("Illuminance (lux)", &light->intensity, 100.0f, 0.0f, 200000.0f);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Ref: Sunlight ~100k, Overcast ~10k, Office ~500");
    } else if (light->type == LightType::Point) {
        ImGui::DragFloat("Luminous Power (lm)", &light->intensity, 10.0f, 0.0f, 10000.0f);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Ref: 100W bulb ~1600lm, 60W ~800lm, Candle ~12lm");
    }

    // Type-specific properties
    if (light->type == LightType::Directional) {
        if (ImGui::DragFloat3("Direction", &light->direction.x, 0.01f, -1.0f, 1.0f)) {
            light->direction = glm::normalize(light->direction);
        }
        if (ImGui::Button("Reset Direction")) {
            light->direction = glm::vec3(-0.3f, -1.0f, -0.3f);
            light->direction = glm::normalize(light->direction);
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "Tip: Use gizmo rotation (R key) to adjust direction visually");
    } else if (light->type == LightType::Point) {
        // Show read-only position info and guidance
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (transform) {
            ImGui::Text("Position: (%.2f, %.2f, %.2f)",
                transform->world.position.x,
                transform->world.position.y,
                transform->world.position.z);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Tip: Use gizmo to adjust position (T) and radius (E)");
        }

        ImGui::Separator();
        ImGui::DragFloat("Radius", &light->radius, 1.0f, 1.0f, 1000.0f);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Radius defines smooth falloff boundary (windowing function)");
    }

    ImGui::Separator();
    ImGui::Checkbox("Enabled", &light->enabled);

    // Delete button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Delete Light")) {
        registry.destroy(entity);
        selectedEntity = entt::null;
        violet::Log::info("UI", "Deleted light entity");
    }
    ImGui::PopStyleColor();
}

void SceneDebugLayer::renderSceneHierarchy() {
    if (!scene || scene->empty()) {
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

                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%u", static_cast<uint32_t>(entity));
                eastl::string entityLabel = buffer;
                if (ImGui::Selectable(entityLabel.c_str(),
                                     entity == selectedEntity, ImGuiSelectableFlags_SpanAllColumns)) {
                    selectedEntity = entity;
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
        return;
    }

    // Show hierarchical tree view of scene nodes
    ImGui::Text("Scene Nodes: %zu", scene->getNodeCount());
    ImGui::Separator();

    // Render root nodes
    for (uint32_t rootId : scene->getRootNodes()) {
        renderSceneNode(rootId);
    }
}

void SceneDebugLayer::renderSceneNode(uint32_t nodeId) {
    const Node* node = scene->getNode(nodeId);
    if (!node) return;

    ImGuiTreeNodeFlags nodeFlags = getNodeFlags(node);

    // Use unique node ID to prevent ImGui ID conflicts (方案1)
    ImGui::PushID(nodeId);
    bool nodeOpen = ImGui::TreeNodeEx("##node", nodeFlags, "%s (ID: %u)",
                                      node->name.empty() ? "Unnamed" : node->name.c_str(), nodeId);

    // Handle node selection
    handleNodeSelection(node);

    // Scene node drag source - enable dragging of this node
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("SCENE_NODE", &nodeId, sizeof(uint32_t));
        ImGui::Text("Reparent: %s", node->name.empty() ? "Unnamed" : node->name.c_str());
        ImGui::EndDragDropSource();
    }

    // Scene node drop target - allow dropping other nodes onto this one
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_NODE")) {
            uint32_t draggedNodeId = *(const uint32_t*)payload->Data;
            handleNodeReparenting(draggedNodeId, nodeId);
        }
        ImGui::EndDragDropTarget();
    }

    // Show entity info if available
    if (node->entity != entt::null) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Entity: %u)", static_cast<uint32_t>(node->entity));

        // Show tooltip with transform info
        renderNodeTooltip(node);
    }

    if (nodeOpen) {
        for (uint32_t childId : node->childrenIds) {
            renderSceneNode(childId);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void SceneDebugLayer::handleNodeSelection(const Node* node) {
    if (ImGui::IsItemClicked() && node->entity != entt::null) {
        selectedEntity = node->entity;
    }
}

void SceneDebugLayer::renderNodeTooltip(const Node* node) {
    if (ImGui::IsItemHovered() && node->entity != entt::null) {
        if (auto* transform = world->getRegistry().try_get<TransformComponent>(node->entity)) {
            ImGui::BeginTooltip();
            ImGui::Text("Local Position: (%.2f, %.2f, %.2f)",
                transform->local.position.x, transform->local.position.y, transform->local.position.z);
            ImGui::Text("World Position: (%.2f, %.2f, %.2f)",
                transform->world.position.x, transform->world.position.y, transform->world.position.z);
            ImGui::EndTooltip();
        }
    }
}

ImGuiTreeNodeFlags SceneDebugLayer::getNodeFlags(const Node* node) const {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

    if (!node->hasChildren()) {
        nodeFlags |= ImGuiTreeNodeFlags_Leaf;
    }

    if (node->entity == selectedEntity) {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
    }

    return nodeFlags;
}

void SceneDebugLayer::handleNodeReparenting(uint32_t draggedNodeId, uint32_t newParentId) {
    // Validate the reparenting operation
    if (!canReparent(draggedNodeId, newParentId)) {
        return;
    }

    const Node* draggedNode = scene->getNode(draggedNodeId);
    if (!draggedNode) {
        return;
    }

    // Preserve world position before reparenting
    preserveWorldPosition(draggedNodeId, newParentId);

    // Perform the reparenting
    scene->setParent(draggedNodeId, newParentId);

    // Update world transforms for the entire scene
    scene->updateWorldTransforms(world->getRegistry());

    // Mark scene as dirty for BVH rebuild
    if (renderer) {
        renderer->markSceneDirty();
    }

    violet::Log::info("UI", "Reparented node {} '{}' to parent {}", draggedNodeId,
            draggedNode->name.empty() ? "Unnamed" : draggedNode->name.c_str(), newParentId);
}

bool SceneDebugLayer::canReparent(uint32_t childId, uint32_t parentId) const {
    // Can't reparent node to itself
    if (childId == parentId) {
        return false;
    }

    // Can't reparent to a non-existent node
    if (parentId != 0 && !scene->getNode(parentId)) {
        return false;
    }

    // Can't reparent node to its own descendant (would create a cycle)
    const Node* currentNode = scene->getNode(parentId);
    while (currentNode && currentNode->parentId != 0) {
        if (currentNode->parentId == childId) {
            return false; // Parent would become a descendant of child
        }
        currentNode = scene->getNode(currentNode->parentId);
    }

    return true;
}

void SceneDebugLayer::preserveWorldPosition(uint32_t nodeId, uint32_t newParentId) {
    const Node* node = scene->getNode(nodeId);
    if (!node || node->entity == entt::null) {
        return;
    }

    auto* transformComp = world->getRegistry().try_get<TransformComponent>(node->entity);
    if (!transformComp) {
        return;
    }

    // Store current world transform
    glm::vec3 worldPosition = transformComp->world.position;
    glm::quat worldRotation = transformComp->world.rotation;
    glm::vec3 worldScale = transformComp->world.scale;

    // Calculate new parent's world transform
    glm::mat4 newParentWorldMatrix = glm::mat4(1.0f);
    if (newParentId != 0) {
        newParentWorldMatrix = scene->getWorldTransform(newParentId, world->getRegistry());
    }

    // Calculate the inverse of new parent's world transform
    glm::mat4 invParentMatrix = glm::inverse(newParentWorldMatrix);

    // Calculate new local transform that will preserve world position
    glm::mat4 currentWorldMatrix = transformComp->world.getMatrix();
    glm::mat4 newLocalMatrix = invParentMatrix * currentWorldMatrix;

    // Decompose the new local matrix back to transform components
    glm::vec3 scale, translation, skew;
    glm::vec4 perspective;
    glm::quat orientation;
    glm::decompose(newLocalMatrix, scale, orientation, translation, skew, perspective);

    // Update local transform
    transformComp->local.position = translation;
    transformComp->local.rotation = orientation;
    transformComp->local.scale = scale;
    transformComp->dirty = true;
}

void SceneDebugLayer::renderEnvironmentPanel() {
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!renderer) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Renderer not available");
            return;
        }

        EnvironmentMap& environmentMap = renderer->getEnvironmentMap();

        // Environment map enable/disable
        bool enabled = environmentMap.isEnabled();
        if (ImGui::Checkbox("Enable Environment Map", &enabled)) {
            environmentMap.setEnabled(enabled);
        }

        ImGui::Separator();

        // Environment map parameters
        float exposure = environmentMap.getExposure();
        if (ImGui::DragFloat("Exposure", &exposure, 0.01f, 0.0f, 0.0f, "%.4f")) {
            environmentMap.setExposure(exposure);
        }

        float rotation = environmentMap.getRotation();
        if (ImGui::SliderFloat("Rotation", &rotation, 0.0f, 6.28318f, "%.2f rad")) {
            environmentMap.setRotation(rotation);
        }

        float intensity = environmentMap.getIntensity();
        if (ImGui::SliderFloat("IBL Intensity", &intensity, 0.0f, 3.0f, "%.2f")) {
            environmentMap.setIntensity(intensity);
        }

        ImGui::Separator();

        // PostProcess tone mapping parameters (Auto-Exposure + EV100 system)
        if (ImGui::CollapsingHeader("Post-Process Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& autoExp = renderer->getAutoExposure();
            auto& params = autoExp.getParams();

            // Auto-exposure toggle
            ImGui::Checkbox("Auto Exposure", &params.enabled);
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Automatically adjust exposure based on scene luminance");
            }

            // Manual EV100 control (when auto-exposure disabled)
            if (!params.enabled) {
                float manualEV = autoExp.getManualEV100();
                if (ImGui::SliderFloat("Manual EV100", &manualEV, -2.0f, 16.0f, "%.1f")) {
                    autoExp.setManualEV100(manualEV);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset##ManualEV")) {
                    autoExp.setManualEV100(9.0f);
                }
            } else {
                // Auto-exposure method selection
                const char* methods[] = { "Simple (Fast)", "Histogram (Accurate)" };
                int currentMethod = static_cast<int>(params.method);
                if (ImGui::Combo("Method", &currentMethod, methods, 2)) {
                    params.method = static_cast<violet::AutoExposureMethod>(currentMethod);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Simple: 256 samples (fast)\nHistogram: Full scene analysis (accurate, UE4/Frostbite standard)");
                }

                // Auto-exposure parameters
                ImGui::SliderFloat("Adaptation Speed", &params.adaptationSpeed, 0.5f, 5.0f, "%.1f");
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How quickly exposure adapts to scene changes");
                }

                ImGui::SliderFloat("Exposure Compensation", &params.exposureCompensation, -4.0f, 4.0f, "%.1f EV");
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Manual offset to auto-computed exposure");
                }

                // Histogram-specific parameters
                if (params.method == violet::AutoExposureMethod::Histogram) {
                    ImGui::Separator();
                    ImGui::Text("Histogram Settings:");
                    ImGui::Indent();

                    ImGui::SliderFloat("Low Percentile", &params.lowPercentile, 0.0f, 0.2f, "%.2f");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Ignore darkest %% of pixels (prevents dark spots from dragging exposure)");
                    }

                    ImGui::SliderFloat("High Percentile", &params.highPercentile, 0.8f, 1.0f, "%.2f");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Ignore brightest %% of pixels (prevents highlights from dragging exposure)");
                    }

                    ImGui::SliderFloat("Center Weight", &params.centerWeightPower, 0.0f, 5.0f, "%.1f");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Weighting for screen center (0 = uniform, 2 = Gaussian-like)");
                    }

                    ImGui::Unindent();
                    ImGui::Separator();
                }

                // Display current/target EV100
                ImGui::Text("Current EV100: %.2f", autoExp.getCurrentEV100());
                ImGui::SameLine();
                ImGui::TextDisabled("Target: %.2f", autoExp.getTargetEV100());
            }

            ImGui::Separator();

            // Tonemap operator selection
            const char* tonemapModes[] = { "ACES Fitted", "ACES Narkowicz", "Uncharted 2", "Reinhard", "None (Linear)" };
            auto& tonemapParams = renderer->getTonemap().getParams();
            int currentMode = static_cast<int>(tonemapParams.mode);
            if (ImGui::Combo("Tone Mapper", &currentMode, tonemapModes, 5)) {
                renderer->getTonemap().setMode(static_cast<TonemapMode>(currentMode));
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "ACES Fitted: UE4/UE5 default, most accurate\n"
                    "ACES Narkowicz: Fast approximation\n"
                    "Uncharted 2: Classic game industry standard\n"
                    "Reinhard: Simple, fast, can wash out\n"
                    "None: Linear (for debugging)");
            }

            ImGui::Separator();

            // Gamma control
            float ppGamma = tonemapParams.gamma;
            if (ImGui::SliderFloat("Gamma", &ppGamma, 1.8f, 2.6f, "%.2f")) {
                renderer->getTonemap().setGamma(ppGamma);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##Gamma")) {
                renderer->getTonemap().setGamma(2.2f);
            }

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "EV Ref: Night -2, Overcast 0, Sunny 9-10, Direct Sun 15");
        }

        ImGui::Separator();

        // HDR file selector with improved functionality
        if (ImGui::Button("Load HDR...")) {
            ImGui::OpenPopup("HDR File Selector");
        }

        // Use the new HDR file selector
        renderHDRFileSelector(environmentMap);

        // Display IBL status
        ImGui::Text("IBL Textures:");
        ImGui::Text("  Environment: %s", environmentMap.getEnvironmentMapIndex() != 0 ? "Loaded" : "None");
        ImGui::Text("  Irradiance: %s", environmentMap.getIrradianceMapIndex() != 0 ? "Generated" : "None");
        ImGui::Text("  Prefiltered: %s", environmentMap.getPrefilteredMapIndex() != 0 ? "Generated" : "None");
        ImGui::Text("  BRDF LUT: %s", environmentMap.getBRDFLUTIndex() != 0 ? "Generated" : "None");

        if (environmentMap.getEnvironmentTexture()) {
            ImGui::SameLine();
            if (ImGui::Button("Generate IBL")) {
                environmentMap.generateIBLMaps();
            }
        }

        // Usage instructions
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Instructions:");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "  • Load HDR file to enable IBL");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "  • Adjust exposure for brightness");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "  • Rotate skybox around Y-axis");
    }
}

void SceneDebugLayer::scanHDRFiles() {
    availableHDRFiles.clear();

    // Scan assets directory recursively for HDR files
    const eastl::string assetsPath = FileSystem::resolveRelativePath("assets");

    eastl::vector<eastl::string> hdrFiles = FileSystem::listFiles(assetsPath, ".hdr", true);

    for (const auto& filePath : hdrFiles) {
        availableHDRFiles.push_back(filePath);
    }

    // Sort HDR files alphabetically for consistent display
    eastl::sort(availableHDRFiles.begin(), availableHDRFiles.end());

    violet::Log::info("UI", "Found {} HDR files in assets directory", availableHDRFiles.size());
}

void SceneDebugLayer::renderHDRFileSelector(EnvironmentMap& environmentMap) {
    if (ImGui::BeginPopup("HDR File Selector")) {
        ImGui::Text("Select HDR Environment Map:");
        ImGui::Separator();

        // Scan for HDR files if list is empty
        if (availableHDRFiles.empty()) {
            scanHDRFiles();
        }

        // Display all found HDR files
        for (const auto& hdrPath : availableHDRFiles) {
            // Extract just the filename for display
            size_t lastSlash = hdrPath.find_last_of('/');
            eastl::string displayName = (lastSlash != eastl::string::npos) ?
                                       hdrPath.substr(lastSlash + 1) : hdrPath;

            if (ImGui::Selectable(displayName.c_str())) {
                violet::Log::info("UI", "Loading HDR file: {}", hdrPath.c_str());
                environmentMap.loadHDR(hdrPath);
                // Automatically generate IBL maps after loading
                environmentMap.generateIBLMaps();
                ImGui::CloseCurrentPopup();
            }

            // Show full path on hover
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", hdrPath.c_str());
            }
        }

        ImGui::Separator();

        // Refresh button
        if (ImGui::Button("Refresh List")) {
            scanHDRFiles();
        }

        ImGui::EndPopup();
    }
}

}