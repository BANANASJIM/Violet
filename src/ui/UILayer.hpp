#pragma once

#include <vulkan/vulkan.hpp>

struct GLFWwindow;

namespace violet {

class VulkanContext;

class UILayer {
public:
    UILayer() = default;
    virtual ~UILayer() = default;

    virtual void onAttach(VulkanContext* context, GLFWwindow* window) {}
    virtual void onDetach() {}
    virtual void onUpdate(float deltaTime) {}
    virtual void onImGuiRender() = 0;

    void beginFrame();
    void endFrame(vk::CommandBuffer commandBuffer);

    bool isInitialized() const { return initialized; }

protected:
    bool initialized{false};
};

}
