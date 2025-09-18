#include "VioletApp.hpp"
#include "scene/SceneLoader.hpp"
#include "core/Log.hpp"
#include "renderer/Mesh.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <EASTL/array.h>
#include <imgui.h>

#include <chrono>

#include "core/TestData.hpp"
#include "core/TestTexture.hpp"

namespace violet {

VioletApp::VioletApp() {
    assetBrowser = eastl::make_unique<AssetBrowserLayer>();
    viewport = eastl::make_unique<ViewportLayer>();
    compositeUI = eastl::make_unique<CompositeUILayer>();

    viewport->setOnAssetDropped([this](const eastl::string& path) {
        VT_INFO("Asset dropped: {}", path.c_str());
        loadAsset(path);
    });

    compositeUI->addLayer(assetBrowser.get());
    compositeUI->addLayer(viewport.get());

    setUILayer(compositeUI.get());
}

VioletApp::~VioletApp() {
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

void VioletApp::createResources() {
    createTestResources();

    renderer.init(getContext(), getRenderPass(), MAX_FRAMES_IN_FLIGHT);

    initializeScene();

    // Test auto-load Sponza scene
    loadAsset("/Users/jim/Dev/Violet/assets/Models/Sponza/glTF/Sponza.gltf");

    // Create default material for all mesh entities that don't have materials
    createDefaultMaterialsForMeshes();
}

void VioletApp::createTestResources() {
    TestTexture::createWhiteTexture(getContext(), defaultTexture);
}


void VioletApp::initializeScene() {
    auto cameraEntity = world.createEntity();

    int width, height;
    glfwGetFramebufferSize(getWindow(), &width, &height);
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    auto camera = eastl::make_unique<PerspectiveCamera>(45.0f, aspectRatio, 0.1f, 50000.0f);

    auto& cameraComp = world.addComponent<CameraComponent>(cameraEntity, eastl::move(camera));
    cameraComp.isActive = true;

    auto controller = eastl::make_unique<CameraController>(cameraComp.camera.get());
    controller->setPosition(glm::vec3(0.0f, 10.0f, 20.0f)); // Y-up: (x, y, z) where y is height
    controller->setMovementSpeed(20.0f);
    controller->setSensitivity(0.002f);

    // Calculate initial look direction towards origin
    glm::vec3 camPos = glm::vec3(0.0f, 10.0f, 20.0f);
    glm::vec3 targetPos = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 direction = glm::normalize(targetPos - camPos);

    // Set yaw and pitch to look at origin
    float yaw = glm::degrees(atan2(direction.z, direction.x));
    float pitch = glm::degrees(asin(-direction.y));
    controller->setYaw(yaw);
    controller->setPitch(pitch);

    VT_INFO("Camera initialized - position: ({:.1f},{:.1f},{:.1f}), yaw: {:.1f}, pitch: {:.1f}",
                 camPos.x, camPos.y, camPos.z, yaw, pitch);

    auto& controllerComp = world.addComponent<CameraControllerComponent>(cameraEntity, eastl::move(controller));
}

void VioletApp::update(float deltaTime) {
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
        VT_DEBUG("Input detected - W:{} A:{} S:{} D:{} Space:{} Shift:{} ImGui wants keyboard:{}",
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
        VT_DEBUG("Mouse input - Right button held, delta:({:.2f},{:.2f}) ImGui wants mouse:{}",
                     delta.x, delta.y, ImGui::GetIO().WantCaptureMouse);
    }
    wasRightHeld = isRightHeld;

    // Update world transforms if scene exists
    if (currentScene) {
        currentScene->updateWorldTransforms(world.getRegistry());
    }
}

void VioletApp::updateUniforms(uint32_t frameIndex) {
    renderer.updateGlobalUniforms(world.getRegistry(), frameIndex);
}

void VioletApp::recordCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) {
    renderer.collectRenderables(world.getRegistry());
    renderer.setViewport(commandBuffer, getSwapchain()->getExtent());
    renderer.renderScene(commandBuffer, getCurrentFrame(), world.getRegistry());
}

void VioletApp::loadAsset(const eastl::string& path) {
    VT_INFO("Loading asset: {}", path.c_str());

    size_t dotPos = path.find_last_of('.');
    if (dotPos != eastl::string::npos) {
        eastl::string ext = path.substr(dotPos);

        if (ext == ".gltf") {
            try {
                // Clear existing scene
                if (currentScene) {
                    currentScene->clear();
                }

                // Load new scene using SceneLoader - now creates ECS entities directly
                currentScene = SceneLoader::loadFromGLTF(getContext(), path, &world.getRegistry());

                // Force all loaded entities to origin position
                auto transformView = world.getRegistry().view<TransformComponent>();
                for (auto entity : transformView) {
                    auto& transform = transformView.get<TransformComponent>(entity);
                    // Only modify non-camera entities (cameras have CameraComponent)
                    if (!world.getRegistry().try_get<CameraComponent>(entity)) {
                        transform.local.position = glm::vec3(0.0f, 0.0f, 0.0f);
                        VT_INFO("Set entity transform position to origin");
                    }
                }

                // Update world transforms for the newly loaded scene
                currentScene->updateWorldTransforms(world.getRegistry());

                VT_INFO("Scene loaded with {} nodes", currentScene->getNodeCount());

                viewport->setStatusMessage("Scene loaded successfully");
                VT_INFO("Scene loaded successfully: {}", path.c_str());
            } catch (const std::exception& e) {
                viewport->setStatusMessage("Failed to load model");
                VT_ERROR("Failed to load model {}: {}", path.c_str(), e.what());
            }
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            viewport->setStatusMessage("Texture loading not implemented");
            VT_INFO("Texture loading not implemented yet: {}", path.c_str());
        }
    }
}

void VioletApp::createDefaultMaterialsForMeshes() {
    // Create a default material
    Material* defaultMaterial = renderer.createMaterial("build/shaders/pbr.vert.spv", "build/shaders/pbr.frag.spv");
    MaterialInstance* defaultMaterialInstance = renderer.createMaterialInstance(defaultMaterial);

    // Set default texture for the material instance
    defaultMaterialInstance->setBaseColorTexture(&defaultTexture);

    // Update descriptor set with texture
    defaultMaterialInstance->updateDescriptorSet(0);

    // Assign default material to all mesh entities that don't have materials
    auto meshView = world.getRegistry().view<MeshComponent>();
    for (auto entity : meshView) {
        if (!world.getRegistry().try_get<MaterialComponent>(entity)) {
            world.addComponent<MaterialComponent>(entity, defaultMaterialInstance);
        }
    }
}


void VioletApp::onWindowResize(int width, int height) {
    if (width > 0 && height > 0) {
        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);

        auto cameraView = world.view<CameraComponent>();
        for (auto entity : cameraView) {
            auto& cameraComp = cameraView.get<CameraComponent>(entity);
            if (cameraComp.isActive && cameraComp.camera) {
                if (auto* perspectiveCamera = dynamic_cast<PerspectiveCamera*>(cameraComp.camera.get())) {
                    perspectiveCamera->setAspectRatio(aspectRatio);
                    VT_DEBUG("Updated camera aspect ratio to {:.3f} ({}x{})", aspectRatio, width, height);
                }
            }
        }
    }
}

void VioletApp::cleanup() {
    if (currentScene) {
        currentScene->cleanup();
    }
    renderer.cleanup();
    defaultTexture.cleanup();
}

} // namespace violet
