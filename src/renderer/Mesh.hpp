#pragma once

#include "renderer/Vertex.hpp"
#include "renderer/IndexBuffer.hpp"
#include "math/AABB.hpp"
#include <EASTL/vector.h>

namespace violet {

struct SubMesh {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
    AABB localBounds;  // Local bounding box for this sub-mesh

    SubMesh() = default;
    SubMesh(uint32_t firstIdx, uint32_t idxCount, uint32_t matIdx = 0)
        : firstIndex(firstIdx), indexCount(idxCount), materialIndex(matIdx) {}

    bool isValid() const { return indexCount > 0; }
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    // Delete copy operations
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Enable move operations
    Mesh(Mesh&&) = default;
    Mesh& operator=(Mesh&&) = default;

    void create(VulkanContext* context,
                const eastl::vector<Vertex>& vertices,
                const eastl::vector<uint32_t>& indices,
                const eastl::vector<SubMesh>& subMeshes);

    void create(VulkanContext* context,
                const eastl::vector<Vertex>& vertices,
                const eastl::vector<uint16_t>& indices,
                const eastl::vector<SubMesh>& subMeshes);

    void cleanup();

    const VertexBuffer& getVertexBuffer() const { return vertexBuffer; }
    const IndexBuffer& getIndexBuffer() const { return indexBuffer; }
    const eastl::vector<SubMesh>& getSubMeshes() const { return subMeshes; }

    size_t getSubMeshCount() const { return subMeshes.size(); }
    const SubMesh& getSubMesh(size_t index) const { return subMeshes[index]; }
    const AABB& getLocalBounds() const { return localBounds; }

private:
    VertexBuffer vertexBuffer;
    IndexBuffer indexBuffer;
    eastl::vector<SubMesh> subMeshes;
    AABB localBounds;  // Will be removed after migration

    void computeBounds(const eastl::vector<Vertex>& vertices);
    void computeSubMeshBounds(const eastl::vector<Vertex>& vertices,
                              const eastl::vector<uint32_t>& indices);
    void computeSubMeshBounds(const eastl::vector<Vertex>& vertices,
                              const eastl::vector<uint16_t>& indices);
};

}
