#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <entt/entt.hpp>

namespace violet {

class VulkanContext;
class RenderGraph;
class ShadowSystem;
class LightingSystem;
class ShaderLibrary;
class GraphicsPipeline;
class DescriptorManager;

class ShadowPass {
public:
    void init(VulkanContext* context, DescriptorManager* descriptorManager, ShaderLibrary* shaderLibrary,
              ShadowSystem* shadowSystem, LightingSystem* lightingSystem,
              RenderGraph* renderGraph, const eastl::string& atlasImageName);
    void cleanup();

    void executePass(vk::CommandBuffer cmd, uint32_t frameIndex, entt::registry& world);

private:
    VulkanContext* context = nullptr;
    DescriptorManager* descriptorManager = nullptr;
    ShaderLibrary* shaderLibrary = nullptr;
    ShadowSystem* shadowSystem = nullptr;
    LightingSystem* lightingSystem = nullptr;
    RenderGraph* renderGraph = nullptr;

    eastl::unique_ptr<GraphicsPipeline> shadowPipeline;
    eastl::string atlasImageName;
};

} // namespace violet