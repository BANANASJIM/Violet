#pragma once

#include <glm/glm.hpp>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/hash_map.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

#include <entt/entt.hpp>

#include "Node.hpp"

namespace violet {

class ResourceManager;
struct GLTFAsset;

class Scene {
public:
    Scene() = default;
    ~Scene();

    // Load scene from glTF file using AssetLoader and ResourceManager
    static eastl::unique_ptr<Scene> loadFromGLTF(
        const eastl::string& filePath,
        ResourceManager& resourceMgr,
        entt::registry& world
    );

    void cleanup();

    uint32_t addNode(const Node& node);
    uint32_t addNode(const eastl::string& name, uint32_t parentId = 0);

    Node* getNode(uint32_t nodeId);
    const Node* getNode(uint32_t nodeId) const;

    bool removeNode(uint32_t nodeId);

    void setParent(uint32_t childId, uint32_t parentId);
    void addChild(uint32_t parentId, uint32_t childId);

    const eastl::vector<uint32_t>& getRootNodes() const { return rootNodeIds; }
    size_t getNodeCount() const { return nodes.size(); }

    void traverseNodes(uint32_t nodeId, eastl::function<void(const Node&)> visitor) const;
    void traverseAllNodes(eastl::function<void(const Node&)> visitor) const;

    void updateWorldTransforms(entt::registry& world);
    glm::mat4 getWorldTransform(uint32_t nodeId, entt::registry& world) const;

    // Coordinate space transformation methods
    glm::mat4 getParentWorldMatrix(uint32_t nodeId, entt::registry& registry) const;
    bool isRootNode(uint32_t nodeId) const;
    glm::mat4 convertWorldToLocal(uint32_t nodeId, const glm::mat4& worldMatrix, entt::registry& registry) const;
    uint32_t findNodeIdForEntity(entt::entity entity) const;

    void clear();
    bool empty() const { return nodes.empty(); }

    // Scene merging functionality
    void mergeScene(const Scene* sourceScene);
    void mergeNodeHierarchy(const Scene* sourceScene, uint32_t sourceNodeId,
                           uint32_t targetParentId, eastl::hash_map<uint32_t, uint32_t>& nodeIdMapping);

private:
    eastl::unordered_map<uint32_t, Node> nodes;
    eastl::vector<uint32_t> rootNodeIds;
    uint32_t nextNodeId = 1;

    void removeFromParent(uint32_t nodeId);
    void updateWorldTransformRecursive(uint32_t nodeId, const glm::mat4& parentTransform, entt::registry& world);
};

} // namespace violet
