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

    // Validate submeshes - only warn once if there are invalid submeshes
    bool hasInvalidSubMesh = false;
    for (const auto& submesh : subMeshes) {
        if (!submesh.isValid()) {
            hasInvalidSubMesh = true;
            break;
        }
    }
    if (hasInvalidSubMesh) {
        VT_WARN("Mesh contains one or more invalid submeshes");
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

    // Validate submeshes - only warn once if there are invalid submeshes
    bool hasInvalidSubMesh = false;
    for (const auto& submesh : subMeshes) {
        if (!submesh.isValid()) {
            hasInvalidSubMesh = true;
            break;
        }
    }
    if (hasInvalidSubMesh) {
        VT_WARN("Mesh contains one or more invalid submeshes");
    }
}

void Mesh::cleanup() {
    vertexBuffer.cleanup();
    indexBuffer.cleanup();
    subMeshes.clear();
}

} // namespace violet
