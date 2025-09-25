#pragma once

#include "UILayer.hpp"
#include "ecs/World.hpp"

namespace violet {

class Renderer;

class SceneDebugLayer : public UILayer {
public:
    SceneDebugLayer(World* world) : world(world), renderer(nullptr) {}
    SceneDebugLayer(World* world, Renderer* renderer) : world(world), renderer(renderer) {}

    void onImGuiRender() override;

private:
    World* world;
    Renderer* renderer;
};

}