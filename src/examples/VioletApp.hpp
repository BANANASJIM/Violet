#pragma once

#include "core/App.hpp"
#include "ecs/World.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/Texture.hpp"
#include "scene/Scene.hpp"
#include "renderer/PerspectiveCamera.hpp"
#include "ui/AssetBrowserLayer.hpp"
#include "ui/ViewportLayer.hpp"
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
    void updateUniforms(uint32_t frameIndex) override;
    void recordCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) override;
    void cleanup() override;
    void onWindowResize(int width, int height) override;

private:
    void createTestResources();
    void initializeScene();
    void loadAsset(const eastl::string& path);
    void createTestCube();

private:
    World world;
    Renderer renderer;

    eastl::unique_ptr<Scene> currentScene;
    Texture defaultTexture;

    eastl::unique_ptr<AssetBrowserLayer> assetBrowser;
    eastl::unique_ptr<ViewportLayer> viewport;
    eastl::unique_ptr<SceneDebugLayer> sceneDebug;
    eastl::unique_ptr<CompositeUILayer> compositeUI;
};

}