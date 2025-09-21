#include "VioletApp.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <EASTL/array.h>
#include <imgui.h>

#include <chrono>

#include "core/Log.hpp"
#include "examples/TestData.hpp"
#include "examples/TestTexture.hpp"
#include "renderer/Mesh.hpp"
#include "scene/SceneLoader.hpp"

namespace violet {

VioletApp::VioletApp() {
    assetBrowser = eastl::make_unique<AssetBrowserLayer>();
    viewport     = eastl::make_unique<ViewportLayer>();
    sceneDebug   = eastl::make_unique<SceneDebugLayer>(&world);
    compositeUI  = eastl::make_unique<CompositeUILayer>();

    viewport->setOnAssetDropped([this](const eastl::string& path) {
        VT_INFO("Asset dropped: {}", path.c_str());
        loadAsset(path);
    });

    compositeUI->addLayer(assetBrowser.get());
    compositeUI->addLayer(viewport.get());
    compositeUI->addLayer(sceneDebug.get());

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
    sceneDebug.reset();
    viewport.reset();
    assetBrowser.reset();

    cleanup();
}

void VioletApp::createResources() {
    createTestResources();

    renderer.init(getContext(), getRenderPass(), MAX_FRAMES_IN_FLIGHT);

    initializeScene();

    eastl::string scenePath = "assets/Models/Sponza/glTF/Sponza.gltf";
    try {
        VT_INFO("Loading default scene: {}", scenePath.c_str());
        currentScene = SceneLoader::loadFromGLTF(getContext(), scenePath, &world.getRegistry(), &renderer, &defaultTexture);

        if (currentScene) {
            currentScene->updateWorldTransforms(world.getRegistry());
            VT_INFO("Scene loaded with {} nodes", currentScene->getNodeCount());

            auto controllerView = world.view<CameraControllerComponent>();
            for (auto entity : controllerView) {
                auto& controllerComp = controllerView.get<CameraControllerComponent>(entity);
                if (controllerComp.controller) {
                    controllerComp.controller->setPosition(glm::vec3(0.0f, 0.0f, 3.0f));
                    controllerComp.controller->setYaw(-90.0f);
                    controllerComp.controller->setPitch(0.0f);
                }
            }
        }
    } catch (const std::exception& e) {
        VT_WARN("Failed to load scene: {}", e.what());
        createTestCube();
    }

    if (!currentScene) {
        createTestCube();
    }
}

void VioletApp::createTestResources() {
    TestTexture::createWhiteTexture(getContext(), defaultTexture);
}


void VioletApp::initializeScene() {
    auto cameraEntity = world.createEntity();

    int width, height;
    glfwGetFramebufferSize(getWindow(), &width, &height);
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    auto  camera      = eastl::make_unique<PerspectiveCamera>(45.0f, aspectRatio, 0.1f, 50000.0f);

    auto& cameraComp    = world.addComponent<CameraComponent>(cameraEntity, eastl::move(camera));
    cameraComp.isActive = true;

    auto controller = eastl::make_unique<CameraController>(cameraComp.camera.get());
    controller->setPosition(glm::vec3(0.0f, 15.0f, 30.0f));
    controller->setMovementSpeed(20.0f);
    controller->setSensitivity(0.002f);

    glm::vec3 camPos = glm::vec3(0.0f, 15.0f, 30.0f);
    glm::vec3 direction = glm::normalize(-camPos);

    float yaw = glm::degrees(atan2(direction.z, direction.x));
    float pitch = glm::degrees(asin(direction.y));
    controller->setYaw(yaw);
    controller->setPitch(pitch);

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

    if (currentScene) {
        currentScene->updateWorldTransforms(world.getRegistry());
    }
}

void VioletApp::updateUniforms(uint32_t frameIndex) {
    renderer.updateGlobalUniforms(world.getRegistry(), frameIndex);
}

void VioletApp::recordCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) {
    renderer.collectRenderables(world.getRegistry());

    auto extent = getSwapchain()->getExtent();
    renderer.setViewport(commandBuffer, extent);
    renderer.renderScene(commandBuffer, getCurrentFrame(), world.getRegistry());
}

void VioletApp::loadAsset(const eastl::string& path) {
    VT_INFO("Loading asset: {}", path.c_str());

    size_t dotPos = path.find_last_of('.');
    if (dotPos != eastl::string::npos) {
        eastl::string ext = path.substr(dotPos);

        if (ext == ".gltf") {
            try {
                if (currentScene) {
                    currentScene->clear();
                }

                currentScene = SceneLoader::loadFromGLTF(getContext(), path, &world.getRegistry(), &renderer, &defaultTexture);
                currentScene->updateWorldTransforms(world.getRegistry());

                viewport->setStatusMessage("Scene loaded successfully");
                VT_INFO("Scene loaded: {}", path.c_str());
            } catch (const std::exception& e) {
                viewport->setStatusMessage("Failed to load model");
                VT_ERROR("Failed to load model {}: {}", path.c_str(), e.what());
            }
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            viewport->setStatusMessage("Texture loading not implemented");
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
                }
            }
        }
    }
}

void VioletApp::createTestCube() {
    auto cubeEntity = world.createEntity();
    auto& transformComp          = world.addComponent<TransformComponent>(cubeEntity);
    transformComp.local.position = glm::vec3(0.0f, 0.0f, 0.0f);
    transformComp.local.scale    = glm::vec3(1.f, 1.0f, 1.0f);

    transformComp.world.position = transformComp.local.position;
    transformComp.world.rotation = transformComp.local.rotation;
    transformComp.world.scale = transformComp.local.scale;
    transformComp.dirty = false;

    auto mesh = eastl::make_unique<Mesh>();
    eastl::vector<Vertex> vertices = TestData::getCubeVertices();
    eastl::vector<uint32_t> indices = TestData::getCubeIndices();
    eastl::vector<SubMesh> subMeshes;
    SubMesh                cubeSubMesh;
    cubeSubMesh.firstIndex    = 0;
    cubeSubMesh.indexCount    = static_cast<uint32_t>(indices.size());
    cubeSubMesh.materialIndex = 0;
    subMeshes.push_back(cubeSubMesh);

    mesh->create(getContext(), vertices, indices, subMeshes);
    auto& meshComp = world.addComponent<MeshComponent>(cubeEntity, eastl::move(mesh));

    eastl::string vertShaderPath = "build/shaders/pbr.vert.spv";
    eastl::string fragShaderPath = "build/shaders/pbr.frag.spv";

    Material* material = renderer.createMaterial(vertShaderPath, fragShaderPath, DescriptorSetType::MaterialTextures);

    if (!material) {
        vertShaderPath = "build/shaders/unlit.vert.spv";
        fragShaderPath = "build/shaders/unlit.frag.spv";
        material = renderer.createMaterial(vertShaderPath, fragShaderPath, DescriptorSetType::UnlitMaterialTextures);
    }

    if (!material) {
        VT_ERROR("Failed to create material");
        return;
    }

    MaterialInstance* materialInstance = nullptr;
    if (vertShaderPath.find("pbr") != eastl::string::npos) {
        materialInstance = renderer.createPBRMaterialInstance(material);
        if (materialInstance) {
            auto* pbrInstance = static_cast<PBRMaterialInstance*>(materialInstance);
            auto& materialData = pbrInstance->getData();
            materialData.baseColorFactor = glm::vec4(0.8f, 0.6f, 0.4f, 1.0f);
            materialData.metallicFactor = 0.1f;
            materialData.roughnessFactor = 0.4f;
            materialData.normalScale = 1.0f;
            materialData.occlusionStrength = 1.0f;
            materialData.emissiveFactor = glm::vec3(0.0f);
            materialData.alphaCutoff = 0.5f;

            pbrInstance->setBaseColorTexture(&defaultTexture);
            pbrInstance->setMetallicRoughnessTexture(&defaultTexture);
            pbrInstance->setNormalTexture(&defaultTexture);
            pbrInstance->setOcclusionTexture(&defaultTexture);
            pbrInstance->setEmissiveTexture(&defaultTexture);
        }
    }

    if (!materialInstance) {
        materialInstance = renderer.createUnlitMaterialInstance(material);
        if (!materialInstance) {
            VT_ERROR("Failed to create material instance");
            return;
        }

        auto* unlitInstance = static_cast<UnlitMaterialInstance*>(materialInstance);
        auto& materialData = unlitInstance->getData();
        materialData.baseColor = glm::vec4(1.0f, 0.5f, 0.3f, 1.0f);
        unlitInstance->setBaseColorTexture(&defaultTexture);
    }

    for (uint32_t frame = 0; frame < 3; ++frame) {
        materialInstance->setDirty(true);
        materialInstance->updateDescriptorSet(frame);
    }

    // Register the material instance with a unique ID for the test cube
    static uint32_t testMaterialId = 0xFFFF0000; // High ID for test materials
    renderer.registerMaterialInstance(testMaterialId, materialInstance);

    // Create material component with the ID
    MaterialComponent matComp;
    matComp.materialIndexToId[0] = testMaterialId; // Single material for SubMesh 0
    world.addComponent<MaterialComponent>(cubeEntity, eastl::move(matComp));
}

void VioletApp::cleanup() {
    if (currentScene) {
        currentScene->cleanup();
    }
    renderer.cleanup();
    defaultTexture.cleanup();
}

} // namespace violet
