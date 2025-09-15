#pragma once

#include "core/App.hpp"
#include "ecs/World.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/Vertex.hpp"
#include "renderer/Texture.hpp"
#include "renderer/Model.hpp"
#include "renderer/PerspectiveCamera.hpp"
#include "ui/AssetBrowserLayer.hpp"
#include "ui/ViewportLayer.hpp"
#include "ui/CompositeUILayer.hpp"
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <entt/entt.hpp>

namespace violet {

class ECSTestApp : public App {
public:
    ECSTestApp();
    ~ECSTestApp() override;

protected:
    void createResources() override;
    void update(float deltaTime) override;
    void updateUniforms(uint32_t frameIndex) override;
    void recordCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) override;
    void cleanup() override;

private:
    void createTestResources();
    void setupDescriptorSets();
    void createEntities();
    void loadAsset(const eastl::string& path);

private:
    World world;

    Pipeline pbrPipeline;
    DescriptorSet descriptorSet;
    eastl::vector<UniformBuffer> uniformBuffers;
    Model gltfModel;
    Texture defaultTexture;

    entt::entity modelEntity;
    entt::entity cameraEntity;

    eastl::unique_ptr<AssetBrowserLayer> assetBrowser;
    eastl::unique_ptr<ViewportLayer> viewport;
    eastl::unique_ptr<CompositeUILayer> compositeUI;
};

}