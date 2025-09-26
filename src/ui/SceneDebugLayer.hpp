#pragma once

#include "UILayer.hpp"
#include "ecs/World.hpp"
#include "input/InputEvents.hpp"
#include "core/events/EventDispatcher.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace violet {

class ForwardRenderer;

class SceneDebugLayer : public UILayer {
public:
    SceneDebugLayer(World* world) : world(world), renderer(nullptr) {}
    SceneDebugLayer(World* world, ForwardRenderer* renderer) : world(world), renderer(renderer) {}
    ~SceneDebugLayer();

    void onAttach(VulkanContext* context, GLFWwindow* window) override;
    void onDetach() override;

    void initialize();
    void shutdown();
    void onImGuiRender() override;

private:
    World* world;
    ForwardRenderer* renderer;

    // Object selection and gizmo members
    entt::entity selectedEntity = entt::null;
    int gizmoOperation = 7; // ImGuizmo::TRANSLATE
    bool enableGizmo = true;

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