#pragma once

#include "core/App.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/Vertex.hpp"
#include "renderer/Texture.hpp"
#include <EASTL/vector.h>

namespace violet {

class TestApp : public App {
public:
    ~TestApp() override;

protected:
    void createResources() override;
    void updateUniforms(uint32_t frameIndex) override;
    void recordCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) override;
    void cleanup() override;

private:
    void createTestResources();
    void setupDescriptorSets();

private:
    Pipeline pipeline;
    DescriptorSet descriptorSet;
    eastl::vector<UniformBuffer> uniformBuffers;
    VertexBuffer cubeVertexBuffer;
    VertexBuffer cubeIndexBuffer;
    Texture testTexture;
};

}