#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

namespace violet {

class VulkanContext;
class RenderGraph;
class ShadowSystem;
class LightingSystem;
class ShaderLibrary;
class Renderable;
class GraphicsPipeline;

class ShadowPass {
public:
    void init(VulkanContext* context, ShaderLibrary* shaderLibrary,
              ShadowSystem* shadowSystem, LightingSystem* lightingSystem,
              RenderGraph* renderGraph, const eastl::string& atlasImageName);
    void cleanup();

    void executePass(vk::CommandBuffer cmd, uint32_t frameIndex,
                     const eastl::vector<Renderable>& renderables);

private:
    VulkanContext* context = nullptr;
    ShaderLibrary* shaderLibrary = nullptr;
    ShadowSystem* shadowSystem = nullptr;
    LightingSystem* lightingSystem = nullptr;
    RenderGraph* renderGraph = nullptr;

    eastl::unique_ptr<GraphicsPipeline> shadowPipeline;
    eastl::string atlasImageName;
};

} // namespace violet