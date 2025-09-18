#pragma once

#include <vulkan/vulkan.hpp>

#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <EASTL/string.h>

#include "renderer/Renderable.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/Material.hpp"

namespace violet {

// Descriptor Set and Binding 约定常量
constexpr uint32_t GLOBAL_SET = 0;    // set = 0: 全局数据(相机、光照)
constexpr uint32_t MATERIAL_SET = 1;  // set = 1: 材质数据(纹理、材质参数)

constexpr uint32_t CAMERA_UBO_BINDING = 0;        // Global set binding 0: 相机变换矩阵
constexpr uint32_t BASE_COLOR_TEXTURE_BINDING = 0; // Material set binding 0: 基础颜色纹理

class VulkanContext;
class Pipeline;
class RenderPass;
class Camera;

struct GlobalUBO {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

class GlobalUniforms {
public:
    ~GlobalUniforms();
    void init(VulkanContext* context, uint32_t maxFramesInFlight);
    void cleanup();
    void update(entt::registry& world, uint32_t frameIndex);
    DescriptorSet* getDescriptorSet() const { return descriptorSet.get(); }

private:
    Camera* findActiveCamera(entt::registry& world);

    VulkanContext* context = nullptr;
    eastl::unique_ptr<DescriptorSet> descriptorSet;
    eastl::vector<eastl::unique_ptr<UniformBuffer>> uniformBuffers;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    Renderer(Renderer&&)            = default;
    Renderer& operator=(Renderer&&) = default;

    void init(VulkanContext* context, RenderPass* renderPass, uint32_t maxFramesInFlight);
    void cleanup();

    void collectRenderables(entt::registry& world);
    void updateGlobalUniforms(entt::registry& world, uint32_t frameIndex);
    void setViewport(vk::CommandBuffer commandBuffer, const vk::Extent2D& extent);
    void renderScene(vk::CommandBuffer commandBuffer, uint32_t frameIndex, entt::registry& world);

    DescriptorSet* getGlobalDescriptorSet() const { return globalUniforms.getDescriptorSet(); }

    Material*         createMaterial(const eastl::string& vertexShader, const eastl::string& fragmentShader);
    MaterialInstance* createMaterialInstance(Material* material);

    void                             clearRenderables() { renderables.clear(); }
    const eastl::vector<Renderable>& getRenderables() const { return renderables; }

private:
    void collectFromEntity(entt::entity entity, entt::registry& world);

    VulkanContext* context    = nullptr;
    RenderPass*    renderPass = nullptr;

    GlobalUniforms globalUniforms;

    eastl::vector<Renderable>                          renderables;
    eastl::vector<eastl::unique_ptr<Material>>         materials;
    eastl::vector<eastl::unique_ptr<MaterialInstance>> materialInstances;

    eastl::unordered_map<entt::entity, eastl::vector<uint32_t>> renderableCache;
};

} // namespace violet
