#pragma once

#include "UILayer.hpp"
#include "ecs/World.hpp"
#include "input/InputEvents.hpp"
#include "core/events/EventDispatcher.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <imgui.h>

namespace violet {

class Scene;
struct Node;

class ForwardRenderer;
class EnvironmentMap;

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

    void setOnAssetDropped(eastl::function<void(const eastl::string&)> callback) {
        onAssetDropped = callback;
    }

    void setOnAssetDroppedWithPosition(eastl::function<void(const eastl::string&, const glm::vec3&)> callback) {
        onAssetDroppedWithPosition = callback;
    }

    // Raycast-based asset placement
    glm::vec3 calculatePlacementPosition(float mouseX, float mouseY);

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

    // Shadow control
    bool shadowsEnabled = true;

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
    void handleAssetDragDrop();

    // Scene hierarchy rendering methods
    void renderSceneHierarchy();
    void renderSceneNode(uint32_t nodeId);
    void handleNodeSelection(const Node* node);
    void renderNodeTooltip(const Node* node);
    ImGuiTreeNodeFlags getNodeFlags(const Node* node) const;

    // Light control window
    void renderLightControlWindow();
    void renderEnvironmentPanel();

    // Helper functions
    Camera* findActiveCamera();
    TransformComponent createInitializedTransform(const glm::vec3& position = glm::vec3(0.0f, 100.0f, 0.0f));
    entt::entity createLightEntity(LightType type, const glm::vec3& position = glm::vec3(0.0f, 100.0f, 0.0f));
    const char* getLightTypeIcon(LightType type);
    const char* getLightTypeString(LightType type);
    void renderLightListItem(entt::entity entity, const LightComponent& light, int index);
    void renderLightProperties(entt::entity entity, LightComponent* light);

    // Scene node reparenting methods
    void handleNodeReparenting(uint32_t draggedNodeId, uint32_t newParentId);
    bool canReparent(uint32_t childId, uint32_t parentId) const;
    void preserveWorldPosition(uint32_t nodeId, uint32_t newParentId);

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

    // Asset drop callbacks
    eastl::function<void(const eastl::string&)> onAssetDropped;
    eastl::function<void(const eastl::string&, const glm::vec3&)> onAssetDroppedWithPosition;

    // HDR file management
    eastl::vector<eastl::string> availableHDRFiles;
    void scanHDRFiles();
    void renderHDRFileSelector(EnvironmentMap& environmentMap);
};

}