#include "VioletApp.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <EASTL/array.h>
#include <imgui.h>


#include "core/Log.hpp"
#include "core/Exception.hpp"
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
        violet::Log::info("App", "Asset dropped at position ({}, {}, {}): {}", position.x, position.y, position.z, path.c_str());
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

    // Initialize renderers with new Pass system
    renderer.init(getContext(), getSwapchain()->getImageFormat(), MAX_FRAMES_IN_FLIGHT);
    debugRenderer.init(getContext(), &renderer.getPass(0), &renderer.getGlobalUniforms(), MAX_FRAMES_IN_FLIGHT);
    debugRenderer.setUILayer(compositeUI.get());

    // Configure App base class
    this->forwardRenderer = &renderer;
    this->App::debugRenderer = &debugRenderer;
    this->App::world = &this->world.getRegistry();

    initializeScene();

    eastl::string scenePath = violet::FileSystem::resolveRelativePath("assets/Models/Sponza/glTF/Sponza.gltf");
    try {
        violet::Log::info("App", "Loading default scene: {}", scenePath.c_str());
        currentScene = SceneLoader::loadFromGLTF(getContext(), scenePath, &world.getRegistry(), &renderer, &defaultTexture);

        if (currentScene) {
            currentScene->updateWorldTransforms(world.getRegistry());
            violet::Log::info("App", "Scene loaded with {} nodes", currentScene->getNodeCount());

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
    } catch (const violet::Exception& e) {
        violet::Log::warn("App", "Failed to load scene: {}", e.what_c_str());
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

    violet::Log::info("App", "Camera positioned at ({:.1f}, {:.1f}, {:.1f}) looking towards scene center",
            camPos.x, camPos.y, camPos.z);
    violet::Log::info("App", "Camera yaw: {:.1f}°, pitch: {:.1f}°", yaw, pitch);

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

// Removed updateUniforms and recordCommands - now handled by Pass system

void VioletApp::loadAsset(const eastl::string& path) {
    violet::Log::info("App", "Loading asset: {}", path.c_str());

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

                // Add default directional light to the scene
                auto lightEntity = world.getRegistry().create();

                // Properly initialize TransformComponent with world transform
                TransformComponent lightTransform;
                lightTransform.local.position = glm::vec3(0.0f, 100.0f, 0.0f);
                lightTransform.world = lightTransform.local;  // Initialize world transform
                lightTransform.dirty = false;
                world.getRegistry().emplace<TransformComponent>(lightEntity, lightTransform);

                auto light = LightComponent::createDirectionalLight(
                    glm::vec3(-0.3f, -1.0f, -0.3f),  // Direction (from sun)
                    glm::vec3(1.0f, 0.95f, 0.8f),     // Warm white color
                    3.0f                              // Intensity
                );
                world.getRegistry().emplace<LightComponent>(lightEntity, light);
                violet::Log::info("App", "Added default directional light to scene");

                // Mark scene dirty for BVH rebuild
                renderer.markSceneDirty();

                violet::Log::info("App", "Scene loaded: {}", path.c_str());
            } catch (const violet::Exception& e) {
                // Failed to load model
                violet::Log::error("App", "Failed to load model {}: {}", path.c_str(), e.what_c_str());
            }
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            // Texture loading not implemented
        }
    }
}


void VioletApp::loadAssetAtPosition(const eastl::string& path, const glm::vec3& position) {
    violet::Log::info("App", "Loading asset at position ({}, {}, {}): {}", position.x, position.y, position.z, path.c_str());

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

                    // Apply position offset to the parent node (which groups the entire model)
                    auto& registry = world.getRegistry();
                    const auto& rootNodeIds = tempScene->getRootNodes();

                    // For imported models with a parent node, we only need to move the parent
                    if (rootNodeIds.size() == 1) {
                        uint32_t rootNodeId = rootNodeIds[0];
                        const Node* node = tempScene->getNode(rootNodeId);

                        // Check if this is a parent node (no entity) or a mesh node
                        if (node) {
                            if (node->entity == entt::null) {
                                // This is a parent grouping node - create a transform entity for it
                                entt::entity parentEntity = world.createEntity();
                                TransformComponent parentTransform;
                                parentTransform.local.setPosition(position);
                                parentTransform.dirty = true;
                                world.addComponent<TransformComponent>(parentEntity, parentTransform);

                                // Update the node to reference this entity
                                const_cast<Node*>(node)->entity = parentEntity;

                                violet::Log::info("App", "Applied position to parent node '{}'", node->name.c_str());
                            } else if (registry.valid(node->entity)) {
                                // Single mesh node - apply position directly
                                auto* transformComp = registry.try_get<TransformComponent>(node->entity);
                                if (transformComp) {
                                    transformComp->local.setPosition(transformComp->local.position + position);
                                    transformComp->dirty = true;
                                }
                            }
                        }
                    } else {
                        // Multiple root nodes - apply to each (legacy behavior)
                        for (uint32_t rootNodeId : rootNodeIds) {
                            const Node* node = tempScene->getNode(rootNodeId);
                            if (node && registry.valid(node->entity)) {
                                auto* transformComp = registry.try_get<TransformComponent>(node->entity);
                                if (transformComp) {
                                    transformComp->local.setPosition(transformComp->local.position + position);
                                    transformComp->dirty = true;
                                }
                            }
                        }
                    }

                    // Merge the temporary scene into the current scene if it exists
                    if (currentScene) {
                        // Use Scene's built-in merge functionality
                        currentScene->mergeScene(tempScene.get());

                        // Update world transforms after merging
                        currentScene->updateWorldTransforms(world.getRegistry());

                        // Update world bounds for all MeshComponents
                        auto view = world.getRegistry().view<TransformComponent, MeshComponent>();
                        for (auto [entity, transformComp, meshComp] : view.each()) {
                            meshComp.updateWorldBounds(transformComp.world.getMatrix());
                        }

                        // Mark scene dirty for BVH rebuild
                        renderer.markSceneDirty();

                        // Update SceneDebug to reflect the merged scene
                        sceneDebug->setScene(currentScene.get());
                    } else {
                        // If no current scene exists, make this the current scene
                        currentScene = eastl::move(tempScene);
                        sceneDebug->setScene(currentScene.get());
                    }

                    violet::Log::info("App", "Asset placed successfully at position ({}, {}, {}): {}", position.x, position.y, position.z, path.c_str());
                }
            } catch (const violet::Exception& e) {
                violet::Log::error("App", "Failed to load asset {}: {}", path.c_str(), e.what_c_str());
            }
        } else if (ext == ".hdr") {
            try {
                // Load HDR file as environment map (skybox)
                violet::Log::info("App", "Loading HDR environment map: {}", path.c_str());
                renderer.getEnvironmentMap().loadHDR(path);
                violet::Log::info("App", "HDR environment map loaded successfully: {}", path.c_str());
            } catch (const violet::Exception& e) {
                violet::Log::error("App", "Failed to load HDR environment map {}: {}", path.c_str(), e.what_c_str());
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
        violet::Log::error("App", "Failed to create PBR material");
        return;
    }

    // Always create PBR material instance
    MaterialInstance* materialInstance = renderer.createPBRMaterialInstance(material);
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
        // Don't set default textures - let PBR instance use its own defaults
    }

    if (!materialInstance) {
        violet::Log::error("App", "Failed to create PBR material instance");
        return;
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
