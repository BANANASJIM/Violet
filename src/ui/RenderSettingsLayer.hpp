#pragma once

#include "UILayer.hpp"
#include "renderer/RenderSettings.hpp"

namespace violet {

class ForwardRenderer;

class RenderSettingsLayer : public UILayer {
public:
    RenderSettingsLayer(ForwardRenderer* renderer);
    ~RenderSettingsLayer() override = default;

    void onAttach(VulkanContext* context, GLFWwindow* window) override;
    void onImGuiRender() override;

private:
    ForwardRenderer* renderer;
    RenderSettings settings;
    float maxDeviceAnisotropy = 16.0f;
};

}