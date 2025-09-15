#include "ECSTestApp.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <EASTL/array.h>
#include <imgui.h>

#include <chrono>

#include "core/TestData.hpp"
#include "core/TestTexture.hpp"

namespace violet {

ECSTestApp::ECSTestApp() {
    assetBrowser = eastl::make_unique<AssetBrowserLayer>();
    viewport = eastl::make_unique<ViewportLayer>();
    compositeUI = eastl::make_unique<CompositeUILayer>();

    viewport->setOnAssetDropped([this](const eastl::string& path) {
        spdlog::info("Asset dropped: {}", path.c_str());
        loadAsset(path);
    });

    compositeUI->addLayer(assetBrowser.get());
    compositeUI->addLayer(viewport.get());

    setUILayer(compositeUI.get());
}

ECSTestApp::~ECSTestApp() {
    // Clear UI layers before destruction to prevent bad access
    if (compositeUI) {
        compositeUI->onDetach();
    }
    setUILayer(nullptr);

    // Clear unique_ptrs in correct order
    compositeUI.reset();
    viewport.reset();
    assetBrowser.reset();

    cleanup();
}

void ECSTestApp::createResources() {
    createTestResources();

    descriptorSet.create(getContext(), MAX_FRAMES_IN_FLIGHT);

    pbrPipeline.init(getContext(), getRenderPass(), &descriptorSet, "build/shaders/pbr.vert.spv",
                     "build/shaders/pbr.frag.spv");

    setupDescriptorSets();
    createEntities();

    // Test auto-load a simple GLTF file to reproduce validation errors
    loadAsset("/Users/jim/Dev/Violet/assets/Models/Box/glTF/Box.gltf");
}

void ECSTestApp::createTestResources() {
    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        uniformBuffers[i].create(getContext(), sizeof(UniformBufferObject));
    }

    TestTexture::createWhiteTexture(getContext(), defaultTexture);
}

void ECSTestApp::setupDescriptorSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        descriptorSet.updateBuffer(i, &uniformBuffers[i]);
        descriptorSet.updateTexture(i, &defaultTexture);
    }
}

void ECSTestApp::createEntities() {
    modelEntity = world.createEntity();

    auto& transform = world.addComponent<Transform>(modelEntity);
    transform.setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    transform.setScale(glm::vec3(1.5f, 1.5f, 1.5f));  // Make the model bigger

    auto& renderable = world.addComponent<Renderable>(modelEntity);
    renderable.pipeline = &pbrPipeline;
    renderable.descriptorSet = &descriptorSet;

    cameraEntity = world.createEntity();

    int width, height;
    glfwGetFramebufferSize(getWindow(), &width, &height);
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    auto camera = eastl::make_unique<PerspectiveCamera>(45.0f, aspectRatio, 0.1f, 100.0f);

    auto& cameraComp = world.addComponent<CameraComponent>(cameraEntity, eastl::move(camera));
    cameraComp.isActive = true;

    auto controller = eastl::make_unique<CameraController>(cameraComp.camera.get());
    controller->setPosition(glm::vec3(2.0f, 2.0f, 2.0f)); // Y-up: (x, y, z) where y is height
    controller->setMovementSpeed(5.0f);
    controller->setSensitivity(0.002f);

    // Calculate initial look direction towards origin
    glm::vec3 camPos = glm::vec3(2.0f, 2.0f, 2.0f);
    glm::vec3 targetPos = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 direction = glm::normalize(targetPos - camPos);

    // Set yaw and pitch to look at origin
    float yaw = glm::degrees(atan2(direction.z, direction.x));
    float pitch = glm::degrees(asin(-direction.y));
    controller->setYaw(yaw);
    controller->setPitch(pitch);

    spdlog::info("Camera initialized - position: ({:.1f},{:.1f},{:.1f}), yaw: {:.1f}, pitch: {:.1f}",
                 camPos.x, camPos.y, camPos.z, yaw, pitch);

    auto& controllerComp = world.addComponent<CameraControllerComponent>(cameraEntity, eastl::move(controller));
}

void ECSTestApp::update(float deltaTime) {
    auto controllerView = world.view<CameraControllerComponent>();
    for (auto entity : controllerView) {
        auto& controllerComp = controllerView.get<CameraControllerComponent>(entity);
        if (controllerComp.controller) {
            controllerComp.controller->update(deltaTime);
        }
    }

    // Debug input detection (only log when keys are actually pressed)
    static bool wasMoving = false;
    bool isMoving = Input::isKeyHeld(GLFW_KEY_W) || Input::isKeyHeld(GLFW_KEY_A) ||
                   Input::isKeyHeld(GLFW_KEY_S) || Input::isKeyHeld(GLFW_KEY_D) ||
                   Input::isKeyHeld(GLFW_KEY_SPACE) || Input::isKeyHeld(GLFW_KEY_LEFT_SHIFT);

    if (isMoving && !wasMoving) {
        spdlog::debug("Input detected - W:{} A:{} S:{} D:{} Space:{} Shift:{} ImGui wants keyboard:{}",
                     Input::isKeyHeld(GLFW_KEY_W), Input::isKeyHeld(GLFW_KEY_A),
                     Input::isKeyHeld(GLFW_KEY_S), Input::isKeyHeld(GLFW_KEY_D),
                     Input::isKeyHeld(GLFW_KEY_SPACE), Input::isKeyHeld(GLFW_KEY_LEFT_SHIFT),
                     ImGui::GetIO().WantCaptureKeyboard);
    }
    wasMoving = isMoving;

    // Log mouse input when right button is held
    static bool wasRightHeld = false;
    bool isRightHeld = Input::isMouseButtonHeld(MouseButton::Right);
    if (isRightHeld && !wasRightHeld) {
        glm::vec2 delta = Input::getMouseDelta();
        spdlog::debug("Mouse input - Right button held, delta:({:.2f},{:.2f}) ImGui wants mouse:{}",
                     delta.x, delta.y, ImGui::GetIO().WantCaptureMouse);
    }
    wasRightHeld = isRightHeld;

    // Model rotation removed - model stays at its initial orientation
}

void ECSTestApp::updateUniforms(uint32_t frameIndex) {
    Camera* activeCamera = nullptr;
    auto cameraView = world.view<CameraComponent>();
    for (auto entity : cameraView) {
        auto& cameraComp = cameraView.get<CameraComponent>(entity);
        if (cameraComp.isActive && cameraComp.camera) {
            activeCamera = cameraComp.camera.get();
            break;
        }
    }

    if (activeCamera && world.hasComponent<Transform>(modelEntity)) {
        const auto& transform = world.getComponent<Transform>(modelEntity);

        UniformBufferObject ubo{};
        ubo.model = transform.getMatrix();
        ubo.view = activeCamera->getViewMatrix();
        ubo.proj = activeCamera->getProjectionMatrix();

        // Removed frequent position logging

        uniformBuffers[frameIndex].update(&ubo, sizeof(ubo));
    }
}

void ECSTestApp::recordCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) {
    auto renderableView = world.view<Renderable>();
    int renderableCount = 0;

    for (auto entity : renderableView) {
        const auto& renderable = renderableView.get<Renderable>(entity);
        renderableCount++;

        if (!renderable.visible || !renderable.pipeline || !renderable.vertexBuffer) {
            continue;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, renderable.pipeline->getPipeline());

        vk::Buffer vertexBuffers[] = {renderable.vertexBuffer->getBuffer()};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        if (renderable.indexBuffer) {
            commandBuffer.bindIndexBuffer(renderable.indexBuffer->getBuffer(), 0, vk::IndexType::eUint32);
        }

        if (renderable.descriptorSet) {
            vk::DescriptorSet currentDescriptorSet = renderable.descriptorSet->getDescriptorSet(getCurrentFrame());
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, renderable.pipeline->getLayout(), 0, 1,
                                             &currentDescriptorSet, 0, nullptr);
        }

        PushConstants pushConstants{};
        pushConstants.baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        pushConstants.metallic = 0.0f;
        pushConstants.roughness = 0.5f;
        pushConstants.normalScale = 1.0f;
        pushConstants.occlusionStrength = 1.0f;
        commandBuffer.pushConstants(renderable.pipeline->getLayout(), vk::ShaderStageFlagBits::eFragment, 0,
                                    sizeof(PushConstants), &pushConstants);

        if (renderable.indexBuffer) {
            uint32_t indexCount = renderable.indexBuffer->getIndexCount();
            commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
        }
    }
}

void ECSTestApp::loadAsset(const eastl::string& path) {
    spdlog::info("Loading asset: {}", path.c_str());
    spdlog::info("Model entity ID: {}", static_cast<uint32_t>(modelEntity));

    size_t dotPos = path.find_last_of('.');
    if (dotPos != eastl::string::npos) {
        eastl::string ext = path.substr(dotPos);

        if (ext == ".gltf") {
            try {
                gltfModel.cleanup();
                gltfModel.loadFromGLTF(getContext(), path);

                if (world.hasComponent<Renderable>(modelEntity)) {
                    auto& renderable = world.getComponent<Renderable>(modelEntity);
                    if (!gltfModel.getMeshes().empty()) {
                        const auto& mesh = gltfModel.getMeshes()[0];
                        renderable.vertexBuffer = const_cast<VertexBuffer*>(&mesh.vertexBuffer);
                        renderable.indexBuffer = const_cast<VertexBuffer*>(&mesh.indexBuffer);
                        spdlog::info("Assigned mesh to entity - vertices exist: {}, indices: {}",
                                    renderable.vertexBuffer != nullptr,
                                    renderable.indexBuffer ? renderable.indexBuffer->getIndexCount() : 0);
                    } else {
                        spdlog::warn("Model has no meshes!");
                    }
                } else {
                    spdlog::error("Model entity has no Renderable component!");
                }

                viewport->setStatusMessage("Model loaded successfully");
                spdlog::info("Model loaded successfully: {}", path.c_str());
            } catch (const std::exception& e) {
                viewport->setStatusMessage("Failed to load model");
                spdlog::error("Failed to load model {}: {}", path.c_str(), e.what());
            }
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            viewport->setStatusMessage("Texture loading not implemented");
            spdlog::info("Texture loading not implemented yet: {}", path.c_str());
        }
    }
}

void ECSTestApp::cleanup() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        uniformBuffers[i].cleanup();
    }

    defaultTexture.cleanup();
    gltfModel.cleanup();
    descriptorSet.cleanup();
    pbrPipeline.cleanup();
}

} // namespace violet
