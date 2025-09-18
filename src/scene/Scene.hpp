#pragma once

#include <glm/glm.hpp>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <entt/entt.hpp>

#include "Node.hpp"

namespace violet {

class Scene {
public:
    Scene() = default;
    ~Scene();

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

    void clear();
    bool empty() const { return nodes.empty(); }

private:
    eastl::unordered_map<uint32_t, Node> nodes;
    eastl::vector<uint32_t> rootNodeIds;
    uint32_t nextNodeId = 1;

    void removeFromParent(uint32_t nodeId);
    void updateWorldTransformRecursive(uint32_t nodeId, const glm::mat4& parentTransform, entt::registry& world);
};

} // namespace violet
