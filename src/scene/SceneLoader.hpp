#pragma once

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>

#include <entt/entt.hpp>

#include "Scene.hpp"
#include "ecs/Components.hpp"

namespace violet {

class VulkanContext;

class SceneLoader {
public:
    static eastl::unique_ptr<Scene> loadFromGLTF(VulkanContext* context, const eastl::string& filePath,
                                                 entt::registry* world);

private:
    static void loadNode(VulkanContext* context, Scene* scene, void* nodePtr, const void* modelPtr, uint32_t parentId,
                         entt::registry* world);
    static void loadTextures(Scene* scene, Node* nodePtr);

    static Transform extractTransform(const void* nodePtr);
    static uint32_t createNodeFromGLTF(Scene* scene, void* nodePtr, uint32_t parentId);
};

} // namespace violet
