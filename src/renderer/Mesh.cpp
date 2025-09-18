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
    } else {
        VT_WARN("Mesh created with empty vertices");
    }

    if (!indices.empty()) {
        indexBuffer.create(context, indices);
    } else {
        VT_WARN("Mesh created with empty indices");
    }

    // Validate submeshes
    for (size_t i = 0; i < subMeshes.size(); ++i) {
        const auto& submesh = subMeshes[i];
        if (!submesh.isValid()) {
            VT_WARN("SubMesh[{}] is invalid (indexCount = 0)", i);
        }
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
    } else {
        VT_WARN("Mesh created with empty vertices");
    }

    if (!indices.empty()) {
        indexBuffer.create(context, indices);
    } else {
        VT_WARN("Mesh created with empty indices");
    }

    // Validate submeshes
    for (size_t i = 0; i < subMeshes.size(); ++i) {
        const auto& submesh = subMeshes[i];
        if (!submesh.isValid()) {
            VT_WARN("SubMesh[{}] is invalid (indexCount = 0)", i);
        }
    }
}

void Mesh::cleanup() {
    vertexBuffer.cleanup();
    indexBuffer.cleanup();
    subMeshes.clear();
}

} // namespace violet
