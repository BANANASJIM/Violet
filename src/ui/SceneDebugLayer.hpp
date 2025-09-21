#pragma once

#include "UILayer.hpp"
#include "ecs/World.hpp"

namespace violet {

class SceneDebugLayer : public UILayer {
public:
    SceneDebugLayer(World* world) : world(world) {}

    void onImGuiRender() override;

private:
    World* world;
};

}