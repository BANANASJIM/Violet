#include "VioletApp.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <EASTL/array.h>
#include <imgui.h>

#include <chrono>

#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include "examples/TestData.hpp"
#include "examples/TestTexture.hpp"
#include "renderer/Mesh.hpp"
#include "scene/SceneLoader.hpp"

namespace violet {

VioletApp::VioletApp() {
    assetBrowser = eastl::make_unique<AssetBrowserLayer>();
    sceneDebug   = eastl::make_unique<SceneDebugLayer>(&world, &renderer);
    compositeUI  = eastl::make_unique<CompositeUILayer>();

    // Set up asset drop callback for scene overlay with position-based placement
    sceneDebug->setOnAssetDroppedWithPosition([this](const eastl::string& path, const glm::vec3& position) {
        VT_INFO("Asset dropped at position ({}, {}, {}): {}", position.x, position.y, position.z, path.c_str());
        loadAssetAtPosition(path, position);
    });

    compositeUI->addLayer(assetBrowser.get());
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
    assetBrowser.reset();

    cleanup();
}

void VioletApp::createResources() {
    createTestResources();

    renderer.init(getContext(), getRenderPass(), MAX_FRAMES_IN_FLIGHT);

    initializeScene();

    eastl::string scenePath = violet::FileSystem::resolveRelativePath("assets/Models/Sponza/glTF/Sponza.gltf");
    try {
        VT_INFO("Loading default scene: {}", scenePath.c_str());
        currentScene = SceneLoader::loadFromGLTF(getContext(), scenePath, &world.getRegistry(), &renderer, &defaultTexture);

        if (currentScene) {
            currentScene->updateWorldTransforms(world.getRegistry());
            VT_INFO("Scene loaded with {} nodes", currentScene->getNodeCount());

            // Update world bounds for all MeshComponents after world transforms are computed
            auto view = world.getRegistry().view<TransformComponent, MeshComponent>();
            for (auto [entity, transformComp, meshComp] : view.each()) {
                meshComp.updateWorldBounds(transformComp.world.getMatrix());
            }

            // Give SceneDebugLayer access to the scene for proper hierarchy handling
            sceneDebug->setScene(currentScene.get());

            // Camera position and orientation are already set correctly in initializeScene()
            // Don't override them here
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
    // Increase far plane to cover Sponza's huge range (~3600 units)
    auto  camera      = eastl::make_unique<PerspectiveCamera>(45.0f, aspectRatio, 1.0f, 5000.0f);

    auto& cameraComp    = world.addComponent<CameraComponent>(cameraEntity, eastl::move(camera));
    cameraComp.isActive = true;

    auto controller = eastl::make_unique<CameraController>(cameraComp.camera.get());
    // Position camera at better distance for Sponza scene viewing
    // Sponza extends from Y:-126 to Y:1347, so position at Y:200 for good overview
    controller->setPosition(glm::vec3(0.0f, 200.0f, 400.0f));
    controller->setMovementSpeed(50.0f);  // Faster movement for large scene
    controller->setSensitivity(0.002f);

    // Look towards scene center (approximately Y:100 based on AABB analysis)
    glm::vec3 camPos = glm::vec3(0.0f, 200.0f, 800.0f);
    glm::vec3 sceneCenter = glm::vec3(0.0f, 100.0f, 0.0f);
    glm::vec3 direction = glm::normalize(sceneCenter - camPos);

    float yaw = glm::degrees(atan2(direction.z, direction.x));
    float pitch = glm::degrees(asin(direction.y));
    controller->setYaw(yaw);
    controller->setPitch(pitch);

    VT_INFO("Camera positioned at ({:.1f}, {:.1f}, {:.1f}) looking towards scene center",
            camPos.x, camPos.y, camPos.z);
    VT_INFO("Camera yaw: {:.1f}°, pitch: {:.1f}°", yaw, pitch);

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

                // Clear old renderables before loading new scene
                renderer.clearRenderables();

                currentScene = SceneLoader::loadFromGLTF(getContext(), path, &world.getRegistry(), &renderer, &defaultTexture);
                currentScene->updateWorldTransforms(world.getRegistry());

                // Update world bounds for all MeshComponents after world transforms are computed
                auto view = world.getRegistry().view<TransformComponent, MeshComponent>();
                for (auto [entity, transformComp, meshComp] : view.each()) {
                    meshComp.updateWorldBounds(transformComp.world.getMatrix());
                }

                // Give SceneDebugLayer access to the scene for proper hierarchy handling
                sceneDebug->setScene(currentScene.get());

                // Mark scene dirty for BVH rebuild
                renderer.markSceneDirty();

                VT_INFO("Scene loaded: {}", path.c_str());
            } catch (const std::exception& e) {
                // Failed to load model
                VT_ERROR("Failed to load model {}: {}", path.c_str(), e.what());
            }
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            // Texture loading not implemented
        }
    }
}

void VioletApp::loadAssetAtPosition(const eastl::string& path, const glm::vec3& position) {
    VT_INFO("Loading asset at position ({}, {}, {}): {}", position.x, position.y, position.z, path.c_str());

    size_t dotPos = path.find_last_of('.');
    if (dotPos != eastl::string::npos) {
        eastl::string ext = path.substr(dotPos);

        if (ext == ".gltf" || ext == ".glb") {
            try {
                // Load the glTF file into a temporary scene to get the entities
                auto tempScene = SceneLoader::loadFromGLTF(getContext(), path, &world.getRegistry(), &renderer, &defaultTexture);

                if (tempScene) {
                    // Update world transforms for the loaded entities
                    tempScene->updateWorldTransforms(world.getRegistry());

                    // Apply position offset to all root entities in the loaded scene
                    auto& registry = world.getRegistry();
                    const auto& rootNodeIds = tempScene->getRootNodes();
                    for (uint32_t rootNodeId : rootNodeIds) {
                        const Node* node = tempScene->getNode(rootNodeId);
                        if (node && registry.valid(node->entity)) {
                            auto* transformComp = registry.try_get<TransformComponent>(node->entity);
                            if (transformComp) {
                                // Apply position offset to the root transform
                                transformComp->local.setPosition(transformComp->local.position + position);
                                transformComp->dirty = true;
                            }
                        }
                    }

                    // Merge the temporary scene into the current scene if it exists
                    if (currentScene) {
                        // Update world transforms after position changes
                        tempScene->updateWorldTransforms(world.getRegistry());

                        // Update world bounds for all MeshComponents
                        auto view = world.getRegistry().view<TransformComponent, MeshComponent>();
                        for (auto [entity, transformComp, meshComp] : view.each()) {
                            meshComp.updateWorldBounds(transformComp.world.getMatrix());
                        }

                        // Mark scene dirty for BVH rebuild
                        renderer.markSceneDirty();
                    } else {
                        // If no current scene exists, make this the current scene
                        currentScene = eastl::move(tempScene);
                        sceneDebug->setScene(currentScene.get());
                    }

                    VT_INFO("Asset placed successfully at position ({}, {}, {}): {}", position.x, position.y, position.z, path.c_str());
                }
            } catch (const std::exception& e) {
                VT_ERROR("Failed to load asset {}: {}", path.c_str(), e.what());
            }
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

    eastl::string vertShaderPath = violet::FileSystem::resolveRelativePath("build/shaders/pbr.vert.spv");
    eastl::string fragShaderPath = violet::FileSystem::resolveRelativePath("build/shaders/pbr.frag.spv");

    Material* material = renderer.createMaterial(vertShaderPath, fragShaderPath, DescriptorSetType::MaterialTextures);

    if (!material) {
        vertShaderPath = violet::FileSystem::resolveRelativePath("build/shaders/unlit.vert.spv");
        fragShaderPath = violet::FileSystem::resolveRelativePath("build/shaders/unlit.frag.spv");
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
