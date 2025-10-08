#include "Scene.hpp"
#include "ecs/Components.hpp"
#include "core/Log.hpp"
#include "asset/AssetLoader.hpp"
#include "asset/GLTFAsset.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Mesh.hpp"
#include "resource/Texture.hpp"
#include "resource/Material.hpp"
#include "resource/MaterialManager.hpp"
#include "renderer/ForwardRenderer.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <algorithm>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace violet {

Scene::~Scene() {
    cleanup();
}

// Synchronous loading - blocks main thread
eastl::unique_ptr<Scene> Scene::loadFromGLTF(
    const eastl::string& filePath,
    ResourceManager& resourceMgr,
    ForwardRenderer& renderer,
    entt::registry& world,
    Texture* defaultTexture
) {
    // Parse glTF file synchronously
    auto asset = AssetLoader::loadGLTF(filePath);
    if (!asset) {
        violet::Log::error("Scene", "Failed to load glTF asset: {}", filePath.c_str());
        return nullptr;
    }

    // Create scene from loaded asset
    return createFromAsset(asset.get(), resourceMgr, renderer, world, filePath, defaultTexture);
}

// Async loading - preferred method
void Scene::loadFromGLTFAsync(
    const eastl::string& filePath,
    ResourceManager& resourceMgr,
    ForwardRenderer& renderer,
    entt::registry& world,
    Texture* defaultTexture,
    eastl::function<void(eastl::unique_ptr<Scene>, eastl::string)> callback
) {
    AssetLoader::loadGLTFAsync(filePath, &resourceMgr,
        [&resourceMgr, &renderer, &world, defaultTexture, filePath, callback]
        (eastl::unique_ptr<GLTFAsset> asset, eastl::string error) {
            if (!error.empty()) {
                callback(nullptr, error);
                return;
            }

            try {
                auto scene = createFromAsset(asset.get(), resourceMgr, renderer, world, filePath, defaultTexture);
                callback(eastl::move(scene), "");
            } catch (const std::exception& e) {
                callback(nullptr, eastl::string(e.what()));
            }
        }
    );
}

void Scene::cleanup() {
    nodes.clear();
    rootNodeIds.clear();
    nextNodeId = 1;
}

uint32_t Scene::addNode(const Node& node) {
    Node newNode = node;
    if (newNode.id == 0) {
        newNode.id = nextNodeId++;
    } else {
        nextNodeId = eastl::max(nextNodeId, newNode.id + 1);
    }

    nodes[newNode.id] = newNode;

    if (newNode.isRoot()) {
        rootNodeIds.push_back(newNode.id);
    } else {
        auto parentIt = nodes.find(newNode.parentId);
        if (parentIt != nodes.end()) {
            parentIt->second.addChild(newNode.id);
        }
    }

    return newNode.id;
}

uint32_t Scene::addNode(const eastl::string& name, uint32_t parentId) {
    Node node(nextNodeId, name);
    node.parentId = parentId;
    return addNode(node);
}

Node* Scene::getNode(uint32_t nodeId) {
    auto it = nodes.find(nodeId);
    return (it != nodes.end()) ? &it->second : nullptr;
}

const Node* Scene::getNode(uint32_t nodeId) const {
    auto it = nodes.find(nodeId);
    return (it != nodes.end()) ? &it->second : nullptr;
}

bool Scene::removeNode(uint32_t nodeId) {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) {
        return false;
    }

    Node& node = nodeIt->second;

    // Reparent children
    for (uint32_t childId : node.childrenIds) {
        auto childIt = nodes.find(childId);
        if (childIt != nodes.end()) {
            childIt->second.parentId = node.parentId;
            if (node.parentId != 0) {
                auto parentIt = nodes.find(node.parentId);
                if (parentIt != nodes.end()) {
                    parentIt->second.addChild(childId);
                }
            } else {
                rootNodeIds.push_back(childId);
            }
        }
    }

    removeFromParent(nodeId);
    nodes.erase(nodeIt);

    return true;
}

void Scene::setParent(uint32_t childId, uint32_t parentId) {
    Node* child = getNode(childId);
    if (!child) {
        return;
    }

    removeFromParent(childId);

    child->parentId = parentId;

    if (parentId == 0) {
        rootNodeIds.push_back(childId);
    } else {
        Node* parent = getNode(parentId);
        if (parent) {
            parent->addChild(childId);
        }
    }
}

void Scene::addChild(uint32_t parentId, uint32_t childId) {
    setParent(childId, parentId);
}


void Scene::traverseNodes(uint32_t nodeId, eastl::function<void(const Node&)> visitor) const {
    const Node* node = getNode(nodeId);
    if (!node) {
        return;
    }

    visitor(*node);

    for (uint32_t childId : node->childrenIds) {
        traverseNodes(childId, visitor);
    }
}

void Scene::traverseAllNodes(eastl::function<void(const Node&)> visitor) const {
    for (uint32_t rootId : rootNodeIds) {
        traverseNodes(rootId, visitor);
    }
}

void Scene::updateWorldTransforms(entt::registry& world) {
    for (uint32_t rootId : rootNodeIds) {
        updateWorldTransformRecursive(rootId, glm::mat4(1.0f), world);
    }
}

glm::mat4 Scene::getWorldTransform(uint32_t nodeId, entt::registry& world) const {
    const Node* node = getNode(nodeId);
    if (!node || node->entity == entt::null) {
        return glm::mat4(1.0f);
    }

    auto* transformComp = world.try_get<TransformComponent>(node->entity);
    if (transformComp) {
        return transformComp->world.getMatrix();
    }

    return glm::mat4(1.0f);
}

void Scene::clear() {
    cleanup();
}

void Scene::updateWorldTransformRecursive(uint32_t nodeId, const glm::mat4& parentTransform, entt::registry& world) {
    const Node* node = getNode(nodeId);
    if (!node || node->entity == entt::null) {
        return;
    }

    auto* transformComp = world.try_get<TransformComponent>(node->entity);
    if (transformComp) {
        glm::mat4 localMatrix = transformComp->local.getMatrix();
        glm::mat4 worldMatrix = parentTransform * localMatrix;

        // Decompose world matrix back to transform components
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat orientation;
        glm::decompose(worldMatrix, scale, orientation, translation, skew, perspective);

        transformComp->world.position = translation;
        transformComp->world.rotation = orientation;
        transformComp->world.scale = scale;
        transformComp->dirty = false;

        // Recursively update children
        for (uint32_t childId : node->childrenIds) {
            updateWorldTransformRecursive(childId, worldMatrix, world);
        }
    }
}

void Scene::removeFromParent(uint32_t nodeId) {
    Node* node = getNode(nodeId);
    if (!node || node->parentId == 0) {
        auto rootIt = eastl::find(rootNodeIds.begin(), rootNodeIds.end(), nodeId);
        if (rootIt != rootNodeIds.end()) {
            rootNodeIds.erase(rootIt);
        }
        return;
    }

    Node* parent = getNode(node->parentId);
    if (parent) {
        parent->removeChild(nodeId);
    }
}

void Scene::mergeScene(const Scene* sourceScene) {
    if (!sourceScene || sourceScene->empty()) {
        return;
    }

    eastl::hash_map<uint32_t, uint32_t> nodeIdMapping;
    const auto& sourceRootNodes = sourceScene->getRootNodes();

    for (uint32_t sourceRootId : sourceRootNodes) {
        mergeNodeHierarchy(sourceScene, sourceRootId, 0, nodeIdMapping);
    }
}

void Scene::mergeNodeHierarchy(const Scene* sourceScene, uint32_t sourceNodeId,
                              uint32_t targetParentId, eastl::hash_map<uint32_t, uint32_t>& nodeIdMapping) {
    const Node* sourceNode = sourceScene->getNode(sourceNodeId);
    if (!sourceNode) return;

    Node newNode = *sourceNode;
    newNode.id = 0;
    newNode.parentId = targetParentId;
    newNode.childrenIds.clear();

    uint32_t newNodeId = addNode(newNode);
    nodeIdMapping[sourceNodeId] = newNodeId;

    //  merge all children
    for (uint32_t childSourceId : sourceNode->childrenIds) {
        mergeNodeHierarchy(sourceScene, childSourceId, newNodeId, nodeIdMapping);
    }
}

glm::mat4 Scene::getParentWorldMatrix(uint32_t nodeId, entt::registry& registry) const {
    const Node* node = getNode(nodeId);
    if (!node || node->parentId == 0) {
        return glm::mat4(1.0f); // Root node's parent transform is identity matrix
    }
    return getWorldTransform(node->parentId, registry);
}

bool Scene::isRootNode(uint32_t nodeId) const {
    const Node* node = getNode(nodeId);
    return node && node->parentId == 0;
}

glm::mat4 Scene::convertWorldToLocal(uint32_t nodeId, const glm::mat4& worldMatrix, entt::registry& registry) const {
    glm::mat4 parentWorldMatrix = getParentWorldMatrix(nodeId, registry);
    return glm::inverse(parentWorldMatrix) * worldMatrix;
}

uint32_t Scene::findNodeIdForEntity(entt::entity entity) const {
    for (const auto& [nodeId, node] : nodes) {
        if (node.entity == entity) {
            return nodeId;
        }
    }
    return 0; // Return 0 if not found (invalid node ID)
}

// Create scene from pre-loaded GLTFAsset
eastl::unique_ptr<Scene> Scene::createFromAsset(
    const GLTFAsset* asset,
    ResourceManager& resourceMgr,
    ForwardRenderer& renderer,
    entt::registry& world,
    const eastl::string& filePath,
    Texture* defaultTexture
) {
    auto scene = eastl::make_unique<Scene>();

    violet::Log::info("Scene", "Creating scene from GLTFAsset: {}", filePath.c_str());
    violet::Log::info("Scene",
        "Nodes: {}, Meshes: {}, Materials: {}, Textures: {}",
        asset->nodes.size(),
        asset->meshes.size(),
        asset->materials.size(),
        asset->textures.size()
    );

    VulkanContext* context = renderer.getContext();
    MaterialManager* materialManager = renderer.getMaterialManager();

    // Step 1: Create GPU textures from asset data
    // 根据glTF 2.0规范，需要判断纹理用途来决定是否使用sRGB：
    // - baseColorTexture, emissiveTexture → sRGB (颜色数据)
    // - normalTexture, metallicRoughnessTexture, occlusionTexture → Linear (数值数据)
    eastl::vector<Texture*> textures(asset->textures.size());

    // 建立texture index到sRGB标志的映射
    eastl::vector<bool> isSRGB(asset->textures.size(), false);

    // 扫描所有材质，标记哪些纹理应该用sRGB
    for (const auto& matData : asset->materials) {
        // baseColorTexture → sRGB
        if (matData.baseColorTexIndex >= 0 && matData.baseColorTexIndex < (int)isSRGB.size()) {
            isSRGB[matData.baseColorTexIndex] = true;
        }
        // emissiveTexture → sRGB
        if (matData.emissiveTexIndex >= 0 && matData.emissiveTexIndex < (int)isSRGB.size()) {
            isSRGB[matData.emissiveTexIndex] = true;
        }
        // normalTexture, metallicRoughnessTexture, occlusionTexture → Linear (默认false)
    }

    // 根据映射加载纹理
    for (size_t i = 0; i < asset->textures.size(); i++) {
        const auto& texData = asset->textures[i];
        auto texture = eastl::make_unique<Texture>();

        if (!texData.pixels.empty()) {
            // Load from embedded data，根据用途使用正确的色彩空间
            texture->loadFromMemory(
                context,
                texData.pixels.data(),
                texData.pixels.size(),
                texData.width,
                texData.height,
                texData.channels,
                isSRGB[i]  // ✅ 根据纹理用途决定sRGB
            );
        } else if (!texData.uri.empty()) {
            // Load from external file，根据纹理用途决定sRGB
            texture->loadFromFile(context, texData.uri, isSRGB[i]);
        }

        texture->setSampler(renderer.getDescriptorManager().getSampler(SamplerType::Default));
        Texture* texturePtr = materialManager->addTexture(eastl::move(texture));
        textures[i] = texturePtr;

        violet::Log::debug("Scene", "Loaded texture {}: sRGB={}", i, isSRGB[i]);
    }

    // Step 2: Create materials
    Material* pbrMaterial = renderer.getPBRBindlessMaterial();
    if (!pbrMaterial) {
        violet::Log::error("Scene", "PBR bindless material not initialized");
        return scene;
    }

    eastl::vector<uint32_t> materialIds(asset->materials.size());
    for (size_t i = 0; i < asset->materials.size(); i++) {
        const auto& matData = asset->materials[i];

        // Create material instance
        MaterialInstanceDesc instanceDesc{
            .material = pbrMaterial,
            .type = MaterialType::PBR,
            .name = matData.name
        };
        uint32_t instanceId = materialManager->createMaterialInstance(instanceDesc);
        PBRMaterialInstance* instance = static_cast<PBRMaterialInstance*>(
            materialManager->getMaterialInstance(instanceId)
        );

        // Set material data
        PBRMaterialData& data = instance->getData();
        data.baseColorFactor = matData.baseColorFactor;
        data.metallicFactor = matData.metallicFactor;
        data.roughnessFactor = matData.roughnessFactor;
        data.normalScale = matData.normalScale;
        data.occlusionStrength = matData.occlusionStrength;
        data.emissiveFactor = matData.emissiveFactor;
        data.alphaCutoff = matData.alphaCutoff;

        // Set alpha mode
        if (matData.alphaMode == "OPAQUE") {
            pbrMaterial->setAlphaMode(Material::AlphaMode::Opaque);
        } else if (matData.alphaMode == "MASK") {
            pbrMaterial->setAlphaMode(Material::AlphaMode::Mask);
        } else if (matData.alphaMode == "BLEND") {
            pbrMaterial->setAlphaMode(Material::AlphaMode::Blend);
        }
        pbrMaterial->setDoubleSided(matData.doubleSided);

        // Assign textures
        auto getTexture = [&](int texIndex) -> Texture* {
            if (texIndex >= 0 && texIndex < static_cast<int>(textures.size()) && textures[texIndex]) {
                return textures[texIndex];
            }
            return defaultTexture;
        };

        instance->setBaseColorTexture(getTexture(matData.baseColorTexIndex));

        // Bindless策略：没有metallicRoughnessTexture时使用默认纹理（bindless index 4）
        // 默认纹理值为(R=0, G=255, B=255)，配合factor使用：
        // metallic = 1.0 * metallicFactor, roughness = 1.0 * roughnessFactor
        instance->setMetallicRoughnessTexture(matData.metallicRoughnessTexIndex >= 0
            ? getTexture(matData.metallicRoughnessTexIndex)
            : materialManager->getDefaultTexture(DefaultTextureType::MetallicRoughness));

        // 没有normalTexture时使用默认法线纹理（bindless index 3）
        instance->setNormalTexture(matData.normalTexIndex >= 0
            ? getTexture(matData.normalTexIndex)
            : materialManager->getDefaultTexture(DefaultTextureType::Normal));

        instance->setOcclusionTexture(matData.occlusionTexIndex >= 0
            ? getTexture(matData.occlusionTexIndex)
            : nullptr);
        instance->setEmissiveTexture(matData.emissiveTexIndex >= 0
            ? getTexture(matData.emissiveTexIndex)
            : nullptr);

        instance->updateMaterialData();

        // Generate unique material ID
        static uint32_t fileIdCounter = 0;
        static eastl::hash_map<eastl::string, uint32_t> fileIdMap;

        uint32_t fileId = 0;
        auto it = fileIdMap.find(filePath);
        if (it == fileIdMap.end()) {
            fileId = ++fileIdCounter;
            fileIdMap[filePath] = fileId;
        } else {
            fileId = it->second;
        }

        uint32_t materialId = (fileId << 16) | static_cast<uint32_t>(i);
        materialManager->registerGlobalMaterial(materialId, instanceId);
        materialIds[i] = materialId;
    }

    // Step 3: Create scene nodes with optional parent grouping
    size_t lastSlash = filePath.find_last_of("/\\");
    size_t lastDot = filePath.find_last_of(".");
    eastl::string modelName = filePath.substr(
        lastSlash != eastl::string::npos ? lastSlash + 1 : 0,
        lastDot != eastl::string::npos ? lastDot - (lastSlash != eastl::string::npos ? lastSlash + 1 : 0) : eastl::string::npos
    );

    uint32_t parentNodeId = 0;
    if (asset->rootNodes.size() > 1 || !modelName.empty()) {
        Node parentNode;
        parentNode.name = modelName.empty() ? "Imported Model" : modelName;
        parentNode.parentId = 0;

        // Create entity with transform for parent node (scale = 1.0)
        auto parentEntity = world.create();
        TransformComponent parentTransform;
        parentTransform.local.position = glm::vec3(0.0f);
        parentTransform.local.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        parentTransform.local.scale = glm::vec3(1.0f);
        parentTransform.world = parentTransform.local;
        parentTransform.dirty = false;
        world.emplace<TransformComponent>(parentEntity, parentTransform);

        parentNode.entity = parentEntity;
        parentNodeId = scene->addNode(parentNode);
        violet::Log::info("Scene", "Created parent node '{}' for imported model", parentNode.name.c_str());
    }

    // Step 4: Create nodes and meshes
    for (uint32_t rootNodeIdx : asset->rootNodes) {
        createNodesFromAsset(scene.get(), asset, resourceMgr, renderer, world, rootNodeIdx, parentNodeId, materialIds);
    }

    violet::Log::info("Scene", "Scene created successfully: {} nodes", scene->getNodeCount());

    // Step 5: Build BVH
    renderer.collectRenderables(world);
    renderer.buildSceneBVH(world);

    return scene;
}

// Helper for creating nodes from asset data
void Scene::createNodesFromAsset(
    Scene* scene,
    const GLTFAsset* asset,
    ResourceManager& resourceMgr,
    ForwardRenderer& renderer,
    entt::registry& world,
    uint32_t nodeIndex,
    uint32_t parentId,
    const eastl::vector<uint32_t>& materialIds
) {
    if (nodeIndex >= asset->nodes.size()) {
        return;
    }

    const auto& nodeData = asset->nodes[nodeIndex];

    // Create node
    Node node;
    node.name = nodeData.name;
    node.parentId = parentId;
    uint32_t nodeId = scene->addNode(node);
    Node* sceneNode = scene->getNode(nodeId);

    // Create ECS entity
    entt::entity entity = world.create();
    sceneNode->entity = entity;

    // Add transform component
    TransformComponent transformComp;
    transformComp.local = nodeData.transform;
    world.emplace<TransformComponent>(entity, transformComp);

    // Create mesh if node has one
    if (nodeData.meshIndex >= 0 && nodeData.meshIndex < static_cast<int>(asset->meshes.size())) {
        const auto& meshData = asset->meshes[nodeData.meshIndex];

        if (!meshData.vertices.empty()) {
            VulkanContext* context = renderer.getContext();

            // Create GPU mesh
            auto meshPtr = eastl::make_unique<Mesh>();
            meshPtr->create(context, meshData.vertices, meshData.indices, meshData.submeshes);
            world.emplace<MeshComponent>(entity, eastl::move(meshPtr));

            // Create material component
            MaterialComponent matComp;
            for (const auto& subMesh : meshData.submeshes) {
                uint32_t gltfMatIndex = subMesh.materialIndex;
                if (gltfMatIndex < materialIds.size()) {
                    matComp.materialIndexToId[gltfMatIndex] = materialIds[gltfMatIndex];
                }
            }
            if (!matComp.materialIndexToId.empty()) {
                world.emplace<MaterialComponent>(entity, eastl::move(matComp));
            }
        }
    }

    // Recursively create child nodes
    for (uint32_t childIdx : nodeData.children) {
        createNodesFromAsset(scene, asset, resourceMgr, renderer, world, childIdx, nodeId, materialIds);
    }
}

}