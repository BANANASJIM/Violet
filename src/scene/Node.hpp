#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/algorithm.h>
#include <entt/entt.hpp>

namespace violet {

struct Node {
    uint32_t id = 0;
    eastl::string name;
    entt::entity entity = entt::null;   // Associated ECS entity

    uint32_t parentId = 0;              // 0 means root node (no parent)
    eastl::vector<uint32_t> childrenIds;

    Node() = default;
    Node(uint32_t nodeId, const eastl::string& nodeName)
        : id(nodeId), name(nodeName) {}

    bool isRoot() const { return parentId == 0; }
    bool hasChildren() const { return !childrenIds.empty(); }

    void addChild(uint32_t childId) {
        childrenIds.push_back(childId);
    }

    void removeChild(uint32_t childId) {
        auto it = eastl::find(childrenIds.begin(), childrenIds.end(), childId);
        if (it != childrenIds.end()) {
            childrenIds.erase(it);
        }
    }
};

}