#include "Mesh.hpp"
#include "core/Log.hpp"

namespace violet {

Mesh::~Mesh() {
    VT_TRACE("Mesh destructor");
    cleanup();
}

void Mesh::create(
    VulkanContext*                 context,
    const eastl::vector<Vertex>&   vertices,
    const eastl::vector<uint32_t>& indices,
    const eastl::vector<SubMesh>&  subMeshes_
) {
    subMeshes = subMeshes_;

    if (!vertices.empty()) {
        vertexBuffer.create(context, vertices);
    }

    if (!indices.empty()) {
        indexBuffer.create(context, indices);
    }
}

void Mesh::create(
    VulkanContext*                 context,
    const eastl::vector<Vertex>&   vertices,
    const eastl::vector<uint16_t>& indices,
    const eastl::vector<SubMesh>&  subMeshes_
) {
    subMeshes = subMeshes_;

    if (!vertices.empty()) {
        vertexBuffer.create(context, vertices);
    }

    if (!indices.empty()) {
        indexBuffer.create(context, indices);
    }
}

void Mesh::cleanup() {
    vertexBuffer.cleanup();
    indexBuffer.cleanup();
    subMeshes.clear();
}

} // namespace violet
