#pragma once

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/hash_map.h>

#include <entt/entt.hpp>

#include "Scene.hpp"
#include "ecs/Components.hpp"

namespace violet {

class VulkanContext;
class Texture;
class Material;
class MaterialInstance;
class ForwardRenderer;

struct GLTFLoadContext {
    VulkanContext* vulkanContext;
    ForwardRenderer* renderer;
    eastl::vector<Texture*> textures;  // Raw pointers - Renderer owns the textures
    eastl::vector<MaterialInstance*> materials;
    eastl::vector<uint32_t> materialIds;  // Unique material IDs for global indexing
    Texture* defaultTexture;
};

class SceneLoader {
public:
    static eastl::unique_ptr<Scene> loadFromGLTF(VulkanContext* context, const eastl::string& filePath,
                                                 entt::registry* world, ForwardRenderer* renderer, Texture* defaultTexture);

private:
    static void loadNode(GLTFLoadContext& loadCtx, Scene* scene, void* nodePtr, const void* modelPtr, uint32_t parentId,
                         entt::registry* world);
    static void loadTextures(GLTFLoadContext& loadCtx, const void* modelPtr);
    static void loadMaterials(GLTFLoadContext& loadCtx, const void* modelPtr, const eastl::string& filePath);

    static Transform extractTransform(const void* nodePtr);
    static uint32_t createNodeFromGLTF(Scene* scene, void* nodePtr, uint32_t parentId);
};

} // namespace violet
