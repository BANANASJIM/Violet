#pragma once

#include "UILayer.hpp"
#include "ecs/World.hpp"
#include "input/InputEvents.hpp"
#include "core/events/EventDispatcher.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace violet {

class Scene;

class ForwardRenderer;

class SceneDebugLayer : public UILayer {
public:
    SceneDebugLayer(World* world) : world(world), renderer(nullptr), scene(nullptr) {}
    SceneDebugLayer(World* world, ForwardRenderer* renderer) : world(world), renderer(renderer), scene(nullptr) {}
    SceneDebugLayer(World* world, ForwardRenderer* renderer, Scene* scene) : world(world), renderer(renderer), scene(scene) {}
    ~SceneDebugLayer();

    void onAttach(VulkanContext* context, GLFWwindow* window) override;
    void onDetach() override;

    void initialize();
    void shutdown();
    void onImGuiRender() override;

    void setScene(Scene* newScene) { scene = newScene; }

private:
    World* world;
    ForwardRenderer* renderer;
    Scene* scene;

    // Object selection and gizmo members
    entt::entity selectedEntity = entt::null;
    int gizmoOperation = 7; // ImGuizmo::TRANSLATE (7)
    bool enableGizmo = true;
    int gizmoMode = 0; // ImGuizmo::WORLD (0), ImGuizmo::LOCAL (1)

    // Snapping settings
    bool enableSnap = false;
    float snapTranslation = 0.5f;  // Translation snap in world units
    float snapRotation = 15.0f;    // Rotation snap in degrees
    float snapScale = 0.1f;        // Scale snap increment

    // Ray debugging members
    bool showRay = false;

    // Fixed ray storage for persistent visualization
    struct StoredRay {
        glm::vec3 origin;
        glm::vec3 direction;
        float length;
    };
    eastl::vector<StoredRay> storedRays;

    // Helper methods
    entt::entity pickObject(float mouseX, float mouseY);
    void renderSelectedEntityOutline();
    void renderRayVisualization();

    // Ray management methods
    void addRayFromMouseClick(float mouseX, float mouseY);
    void clearAllRays();
    void renderGizmo();

public:
    // Ray access for renderer
    const eastl::vector<StoredRay>& getStoredRays() const { return storedRays; }

private:

    // Event handlers
    bool onMousePressed(const MousePressedEvent& event);
    bool onKeyPressed(const KeyPressedEvent& event);

    // Event subscription tracking
    EventDispatcher::HandlerId mouseClickHandlerId = 0;
    EventDispatcher::HandlerId keyPressHandlerId = 0;
};

}