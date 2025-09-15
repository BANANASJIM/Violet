#pragma once

#include "renderer/Vertex.hpp"
#include <EASTL/vector.h>

namespace violet {

struct Primitive {
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t materialIndex{-1};
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh() = default;

    void create(VulkanContext* context,
                const eastl::vector<Vertex>& vertices,
                const eastl::vector<uint32_t>& indices,
                const eastl::vector<Primitive>& primitives);

    void cleanup();

    const VertexBuffer& getVertexBuffer() const { return vertexBuffer; }
    const VertexBuffer& getIndexBuffer() const { return indexBuffer; }
    const eastl::vector<Primitive>& getPrimitives() const { return primitives; }

private:
    VertexBuffer vertexBuffer;
    VertexBuffer indexBuffer;
    eastl::vector<Primitive> primitives;
};

}
