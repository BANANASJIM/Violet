#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace violet {

class Mesh;
class Material;

struct Renderable {
    Mesh* mesh = nullptr;
    Material* material = nullptr;
    glm::mat4 worldTransform{1.0f};
    uint32_t subMeshIndex = 0;
    bool visible = true;
    bool dirty = true;
    entt::entity entity = entt::null;

    Renderable() = default;

    Renderable(entt::entity e, Mesh* m, Material* mat, const glm::mat4& transform, uint32_t subMesh = 0)
        : mesh(m), material(mat), worldTransform(transform), subMeshIndex(subMesh), entity(e) {}
};

} // namespace violet