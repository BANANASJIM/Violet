#pragma once

#include "core/App.hpp"
#include "ecs/World.hpp"
#include "renderer/core/ForwardRenderer.hpp"
#include "resource/ResourceManager.hpp"
#include "renderer/core/DebugRenderer.hpp"
#include "resource/Texture.hpp"
#include "scene/Scene.hpp"
#include "renderer/PerspectiveCamera.hpp"
#include "ui/AssetBrowserLayer.hpp"
#include "ui/CompositeUILayer.hpp"
#include "ui/SceneDebugLayer.hpp"
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <entt/entt.hpp>

namespace violet {

class VioletApp : public App {
public:
    VioletApp();
    ~VioletApp() override;

protected:
    void createResources() override;
    void update(float deltaTime) override;
    void cleanup() override;
    void onWindowResize(int width, int height) override;

private:
    void initializeScene();
    void loadAsset(const eastl::string& path);
    void loadAssetAtPosition(const eastl::string& path, const glm::vec3& position);

private:
    World world;
    ResourceManager resourceManager;  // Unified resource management
    ForwardRenderer renderer;
    DebugRenderer debugRenderer;

    eastl::unique_ptr<Scene> currentScene;

    eastl::unique_ptr<AssetBrowserLayer> assetBrowser;
    eastl::unique_ptr<SceneDebugLayer> sceneDebug;
    eastl::unique_ptr<CompositeUILayer> compositeUI;
};

}