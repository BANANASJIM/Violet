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

    computeBounds(vertices);
    computeSubMeshBounds(vertices, indices);

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

    computeBounds(vertices);
    computeSubMeshBounds(vertices, indices);

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
    localBounds.reset();
}

void Mesh::computeBounds(const eastl::vector<Vertex>& vertices) {
    localBounds.reset();
    for (const auto& vertex : vertices) {
        localBounds.expand(vertex.pos);
    }
}

void Mesh::computeSubMeshBounds(const eastl::vector<Vertex>& vertices,
                                const eastl::vector<uint32_t>& indices) {
    for (auto& subMesh : subMeshes) {
        subMesh.localBounds.reset();

        // Calculate bounds for this specific submesh
        uint32_t endIndex = subMesh.firstIndex + subMesh.indexCount;
        for (uint32_t i = subMesh.firstIndex; i < endIndex; ++i) {
            if (i < indices.size()) {
                uint32_t vertexIndex = indices[i];
                if (vertexIndex < vertices.size()) {
                    subMesh.localBounds.expand(vertices[vertexIndex].pos);
                }
            }
        }

        VT_DEBUG("SubMesh bounds: min({:.2f}, {:.2f}, {:.2f}) max({:.2f}, {:.2f}, {:.2f})",
                 subMesh.localBounds.min.x, subMesh.localBounds.min.y, subMesh.localBounds.min.z,
                 subMesh.localBounds.max.x, subMesh.localBounds.max.y, subMesh.localBounds.max.z);
    }
}

void Mesh::computeSubMeshBounds(const eastl::vector<Vertex>& vertices,
                                const eastl::vector<uint16_t>& indices) {
    for (auto& subMesh : subMeshes) {
        subMesh.localBounds.reset();

        // Calculate bounds for this specific submesh
        uint32_t endIndex = subMesh.firstIndex + subMesh.indexCount;
        for (uint32_t i = subMesh.firstIndex; i < endIndex; ++i) {
            if (i < indices.size()) {
                uint16_t vertexIndex = indices[i];
                if (vertexIndex < vertices.size()) {
                    subMesh.localBounds.expand(vertices[vertexIndex].pos);
                }
            }
        }

        VT_DEBUG("SubMesh bounds (16bit): min({:.2f}, {:.2f}, {:.2f}) max({:.2f}, {:.2f}, {:.2f})",
                 subMesh.localBounds.min.x, subMesh.localBounds.min.y, subMesh.localBounds.min.z,
                 subMesh.localBounds.max.x, subMesh.localBounds.max.y, subMesh.localBounds.max.z);
    }
}

} // namespace violet
