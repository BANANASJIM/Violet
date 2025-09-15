#include "Mesh.hpp"

namespace violet {

void Mesh::create(VulkanContext* context,
                  const eastl::vector<Vertex>& vertices,
                  const eastl::vector<uint32_t>& indices,
                  const eastl::vector<Primitive>& prims) {
    primitives = prims;

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
    primitives.clear();
}

}
