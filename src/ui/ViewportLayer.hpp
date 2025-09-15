#pragma once

#include "UILayer.hpp"
#include <EASTL/functional.h>
#include <EASTL/string.h>

namespace violet {

class ViewportLayer : public UILayer {
public:
    ViewportLayer() = default;
    ~ViewportLayer() override = default;

    void onAttach(VulkanContext* context, GLFWwindow* window) override;
    void onDetach() override;
    void onImGuiRender() override;

    void setOnAssetDropped(eastl::function<void(const eastl::string&)> callback) {
        onAssetDropped = callback;
    }

    void setStatusMessage(const eastl::string& message) {
        statusMessage = message;
    }

private:
    eastl::function<void(const eastl::string&)> onAssetDropped;
    eastl::string statusMessage = "Drop GLTF files here to load";
};

}