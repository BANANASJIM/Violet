#pragma once

#include "UILayer.hpp"
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

namespace violet {

class CompositeUILayer : public UILayer {
public:
    CompositeUILayer() = default;
    ~CompositeUILayer() override = default;

    void addLayer(UILayer* layer) {
        layers.push_back(layer);
    }

    void onAttach(VulkanContext* context, GLFWwindow* window) override {
        UILayer::onAttach(context, window);
        for (auto layer : layers) {
            layer->onAttach(context, window);
        }
        initialized = true;
    }

    void onDetach() override {
        // Call onDetach only if layers are still valid
        for (auto layer : layers) {
            if (layer) {
                layer->onDetach();
            }
        }
        layers.clear();  // Clear the vector to prevent double-deletion
        UILayer::onDetach();
    }

    void onUpdate(float deltaTime) override {
        for (auto layer : layers) {
            layer->onUpdate(deltaTime);
        }
    }

    void onImGuiRender() override {
        for (auto layer : layers) {
            layer->onImGuiRender();
        }
    }

private:
    eastl::vector<UILayer*> layers;
};

}