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
        VT_INFO("Started dragging asset");
        wasDragging = true;
    } else if (!isDragging && wasDragging) {
        VT_INFO("Stopped dragging asset");
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
            VT_DEBUG("Drop target ready");

            if (const ImGuiPayload* dropPayload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                const char* path = (const char*)dropPayload->Data;
                VT_INFO("Asset dropped in scene: {}", path);

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

                eastl::string entityLabel = std::to_string(static_cast<uint32_t>(entity)).c_str();
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

    VT_INFO("Reparented node {} '{}' to parent {}", draggedNodeId,
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

}