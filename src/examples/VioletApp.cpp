#include "VioletApp.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <EASTL/array.h>
#include <imgui.h>


#include "core/Log.hpp"
#include "core/Exception.hpp"
#include "core/FileSystem.hpp"
#include "resource/Mesh.hpp"
#include "scene/Scene.hpp"

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

    resourceManager.cleanup();
    // NOTE: Do NOT call cleanup() here - App::~App() already calls it via internalCleanup()
}

void VioletApp::createResources() {
    // 1. Initialize ResourceManager first (includes DescriptorManager initialization)
    resourceManager.init(getContext(), MAX_FRAMES_IN_FLIGHT);
    resourceManager.createDefaultResources();

    // 2. Initialize Renderer (all dependencies are ready - DescriptorManager, MaterialManager, etc.)
    renderer.init(getContext(), &resourceManager, getSwapchain()->getImageFormat(), MAX_FRAMES_IN_FLIGHT);

    // Set swapchain for RenderGraph
    renderer.setSwapchain(getSwapchain());

    // TODO: debugRenderer needs update for dynamic rendering
    // debugRenderer.init(getContext(), ..., getSwapchain()->getImageFormat(), MAX_FRAMES_IN_FLIGHT);
    // debugRenderer.setUILayer(compositeUI.get());

    // Note: initAutoExposure() no longer needed - auto-exposure is initialized in renderer.init()

    // Configure App base class
    this->forwardRenderer = &renderer;
    // this->App::debugRenderer = &debugRenderer;  // TODO: Re-enable after debugRenderer migrated
    this->App::world = &this->world.getRegistry();

    initializeScene();

    // Load default scene asynchronously (non-blocking)
    eastl::string scenePath = violet::FileSystem::resolveRelativePath("assets/Models/Sponza/glTF/Sponza.gltf");
    violet::Log::info("App", "Loading default scene asynchronously: {}", scenePath.c_str());

    Scene::loadFromGLTFAsync(
        scenePath,
        resourceManager,
        renderer,
        world.getRegistry(),
        resourceManager.getTextureManager()->getDefaultTexture(DefaultTextureType::White),
        [this](eastl::unique_ptr<Scene> scene, eastl::string error) {
            if (!error.empty()) {
                violet::Log::error("App", "Failed to load scene: {}", error.c_str());
                return;
            }

            try {
                currentScene = eastl::move(scene);

                if (currentScene) {
                    currentScene->updateWorldTransforms(world.getRegistry());
                    violet::Log::info("App", "Scene loaded with {} nodes", currentScene->getNodeCount());

                    // Update world bounds for all MeshComponents after world transforms are computed
                    auto view = world.getRegistry().view<TransformComponent, MeshComponent>();
                    for (auto&& [entity, transformComp, meshComp] : view.each()) {
                        meshComp.updateWorldBounds(transformComp.world.getMatrix());
                    }

                    // Give SceneDebugLayer access to the scene for proper hierarchy handling
                    sceneDebug->setScene(currentScene.get());

                    // Camera position and orientation are already set correctly in initializeScene()
                    // Don't override them here
                }
            } catch (const violet::Exception& e) {
                violet::Log::error("App", "Failed to create scene from asset: {}", e.what_c_str());
            }
        }
    );

    // Load default HDR environment map
    eastl::string defaultHDR = violet::FileSystem::resolveRelativePath("assets/textures/stadium_exterior_4k.hdr");
    violet::Log::info("App", "Loading default HDR environment map: {}", defaultHDR.c_str());
    renderer.getEnvironmentMap().loadHDR(defaultHDR);
    renderer.getEnvironmentMap().generateIBLMaps();
    violet::Log::info("App", "Default HDR environment map loaded successfully");
}

// createTestResources removed - using MaterialManager default textures instead


void VioletApp::initializeScene() {
    auto cameraEntity = world.createEntity();

    int width, height;
    glfwGetFramebufferSize(getWindow(), &width, &height);
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    auto  camera      = eastl::make_unique<PerspectiveCamera>(45.0f, aspectRatio, 0.1f, 5000.0f);

    auto& cameraComp    = world.addComponent<CameraComponent>(cameraEntity, eastl::move(camera));
    cameraComp.isActive = true;

    auto controller = eastl::make_unique<CameraController>(cameraComp.camera.get());
    // Position camera for Sponza viewing (large indoor scene)
    controller->setPosition(glm::vec3(-10.0f, 5.0f, 0.0f));
    controller->setMovementSpeed(5.0f);  // Faster movement for large scene
    controller->setSensitivity(0.002f);

    // Look towards scene center
    glm::vec3 camPos = glm::vec3(-10.0f, 5.0f, 0.0f);
    glm::vec3 sceneCenter = glm::vec3(0.0f, 5.0f, 0.0f);
    glm::vec3 direction = glm::normalize(sceneCenter - camPos);

    float yaw = glm::degrees(atan2(direction.z, direction.x));
    float pitch = glm::degrees(asin(direction.y));
    controller->setYaw(yaw);
    controller->setPitch(pitch);

    auto& controllerComp = world.addComponent<CameraControllerComponent>(cameraEntity, eastl::move(controller));

    // Add default directional light for better material visualization
    auto lightEntity = world.getRegistry().create();
    TransformComponent lightTransform;
    lightTransform.local.position = glm::vec3(0.0f, 100.0f, 0.0f);
    lightTransform.world = lightTransform.local;
    lightTransform.dirty = false;
    world.getRegistry().emplace<TransformComponent>(lightEntity, lightTransform);

    auto light = LightComponent::createDirectionalLight(
        glm::vec3(-0.3f, -1.0f, -0.3f),  // Direction from upper-left
        glm::vec3(1.0f, 0.95f, 0.8f),    // Warm white color
        30.0f                          // Illuminance in lux
    );
    world.getRegistry().emplace<LightComponent>(lightEntity, light);

    violet::Log::info("App", "Created default directional light");
}

void VioletApp::update(float deltaTime) {
    // Process completed async loading tasks
    resourceManager.processAsyncTasks();

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

void VioletApp::loadAsset(const eastl::string& path) {
    violet::Log::info("App", "Loading asset asynchronously: {}", path.c_str());

    size_t dotPos = path.find_last_of('.');
    if (dotPos != eastl::string::npos) {
        eastl::string ext = path.substr(dotPos);

        if (ext == ".gltf") {
            // Use async loading
            Scene::loadFromGLTFAsync(
                path,
                resourceManager,
                renderer,
                world.getRegistry(),
                resourceManager.getTextureManager()->getDefaultTexture(DefaultTextureType::White),
                [this, path](eastl::unique_ptr<Scene> scene, eastl::string error) {
                    if (!error.empty()) {
                        violet::Log::error("App", "Failed to load glTF {}: {}", path.c_str(), error.c_str());
                        return;
                    }

                    try {
                        if (currentScene) {
                            currentScene->clear();
                        }

                        // Clear old renderables before loading new scene
                        renderer.clearRenderables();

                        // Use loaded scene
                        currentScene = eastl::move(scene);

                        currentScene->updateWorldTransforms(world.getRegistry());

                    // Update world bounds for all MeshComponents
                    auto view = world.getRegistry().view<TransformComponent, MeshComponent>();
                    for (auto&& [entity, transformComp, meshComp] : view.each()) {
                        meshComp.updateWorldBounds(transformComp.world.getMatrix());
                    }

                    sceneDebug->setScene(currentScene.get());

                    // Add default directional light
                    auto lightEntity = world.getRegistry().create();
                    TransformComponent lightTransform;
                    lightTransform.local.position = glm::vec3(0.0f, 100.0f, 0.0f);
                    lightTransform.world = lightTransform.local;
                    lightTransform.dirty = false;
                    world.getRegistry().emplace<TransformComponent>(lightEntity, lightTransform);

                    auto light = LightComponent::createDirectionalLight(
                        glm::vec3(-0.3f, -1.0f, -0.3f),
                        glm::vec3(1.0f, 0.95f, 0.8f),
                        30000.0f  // Illuminance in lux
                    );
                    world.getRegistry().emplace<LightComponent>(lightEntity, light);

                    renderer.markSceneDirty();

                    violet::Log::info("App", "Scene loaded asynchronously: {}", path.c_str());
                } catch (const violet::Exception& e) {
                    violet::Log::error("App", "Failed to create scene from asset {}: {}", path.c_str(), e.what_c_str());
                }
            });
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
            // Load asset asynchronously
            Scene::loadFromGLTFAsync(
                path,
                resourceManager,
                renderer,
                world.getRegistry(),
                resourceManager.getTextureManager()->getDefaultTexture(DefaultTextureType::White),
                [this, path, position](eastl::unique_ptr<Scene> tempScene, eastl::string error) {
                    if (!error.empty()) {
                        violet::Log::error("App", "Failed to load asset {}: {}", path.c_str(), error.c_str());
                        return;
                    }

                    try {
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
                        // Multiple root nodes - apply to each
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
                        for (auto&& [entity, transformComp, meshComp] : view.each()) {
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
                        violet::Log::error("App", "Failed to create scene from asset {}: {}", path.c_str(), e.what_c_str());
                    }
                }
            );
        } else if (ext == ".hdr") {
            try {
                // Load HDR file as environment map (skybox)
                violet::Log::info("App", "Loading HDR environment map: {}", path.c_str());
                renderer.getEnvironmentMap().loadHDR(path);
                // Automatically generate IBL maps after loading
                renderer.getEnvironmentMap().generateIBLMaps();
                violet::Log::info("App", "HDR environment map loaded and IBL generated successfully: {}", path.c_str());
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

void VioletApp::cleanup() {
    if (currentScene) {
        currentScene->cleanup();
    }

    // Cleanup renderers (ResourceManager already cleaned in destructor)
    renderer.cleanup();
    debugRenderer.cleanup();
}

} // namespace violet
