#include "TestApp.hpp"
#include "core/TestData.hpp"
#include "core/TestTexture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <EASTL/array.h>

namespace violet {

TestApp::~TestApp() {
    cleanup();
}

void TestApp::createResources() {
    createTestResources();

    descriptorSet.create(getContext(), MAX_FRAMES_IN_FLIGHT);

    pipeline.init(getContext(), getRenderPass(), &descriptorSet,
                  "build/shaders/model.vert.spv", "build/shaders/model.frag.spv");

    setupDescriptorSets();
}

void TestApp::createTestResources() {
    auto vertices = TestData::getCubeVertices();
    auto indices = TestData::getCubeIndices();

    cubeVertexBuffer.create(getContext(), vertices);
    cubeIndexBuffer.create(getContext(), indices);

    TestTexture::createCheckerboardTexture(getContext(), testTexture);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        uniformBuffers[i].create(getContext(), sizeof(UniformBufferObject));
    }

    int width, height;
    glfwGetFramebufferSize(getWindow(), &width, &height);
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    camera = eastl::make_unique<PerspectiveCamera>(45.0f, aspectRatio, 0.1f, 10.0f);
    camera->setPosition(glm::vec3(3.0f, 3.0f, 3.0f));
    camera->setTarget(glm::vec3(0.0f, 0.0f, 0.0f));
    camera->setUp(glm::vec3(0.0f, 0.0f, 1.0f));
}

void TestApp::setupDescriptorSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        descriptorSet.updateBuffer(i, &uniformBuffers[i]);
        descriptorSet.updateTexture(i, &testTexture);
    }
}

void TestApp::updateUniforms(uint32_t frameIndex) {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(
        currentTime - startTime).count();

    UniformBufferObject ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = camera->getViewMatrix();
    ubo.proj = camera->getProjectionMatrix();

    uniformBuffers[frameIndex].update(&ubo, sizeof(ubo));
}

void TestApp::recordCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());

    vk::Buffer vertexBuffers[] = {cubeVertexBuffer.getBuffer()};
    vk::DeviceSize offsets[] = {0};
    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

    commandBuffer.bindIndexBuffer(cubeIndexBuffer.getBuffer(), 0, vk::IndexType::eUint32);

    vk::DescriptorSet currentDescriptorSet = descriptorSet.getDescriptorSet(getCurrentFrame());
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.getLayout(),
                                     0, 1, &currentDescriptorSet, 0, nullptr);

    PushConstants pushConstants{};
    pushConstants.baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    pushConstants.metallic = 0.0f;
    pushConstants.roughness = 0.5f;
    pushConstants.normalScale = 1.0f;
    pushConstants.occlusionStrength = 1.0f;
    commandBuffer.pushConstants(pipeline.getLayout(), vk::ShaderStageFlagBits::eFragment,
                                0, sizeof(PushConstants), &pushConstants);

    commandBuffer.drawIndexed(cubeIndexBuffer.getIndexCount(), 1, 0, 0, 0);
}

void TestApp::cleanup() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        uniformBuffers[i].cleanup();
    }

    cubeVertexBuffer.cleanup();
    cubeIndexBuffer.cleanup();
    testTexture.cleanup();
    descriptorSet.cleanup();
    pipeline.cleanup();
}

void TestApp::onWindowResize(int width, int height) {
    if (camera && width > 0 && height > 0) {
        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        camera->setAspectRatio(aspectRatio);
    }
}

}