#include "Scene.hpp"
#include "ecs/Components.hpp"
#include "core/Log.hpp"
#include <algorithm>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace violet {

Scene::~Scene() {
    cleanup();
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

}