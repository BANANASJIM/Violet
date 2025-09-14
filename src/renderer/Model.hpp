#pragma once

#include "Vertex.hpp"
#include "Texture.hpp"
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <glm/glm.hpp>

namespace violet {

class VulkanContext;

struct Mesh {
    VertexBuffer vertexBuffer;
    VertexBuffer indexBuffer;
    Texture* texture = nullptr;
    uint32_t indexCount = 0;
};

class Model {
public:
    void loadFromGLTF(VulkanContext* context, const eastl::string& filePath);
    void cleanup();

    const eastl::vector<Mesh>& getMeshes() const { return meshes; }

private:
    void loadNode(VulkanContext* context, void* node, const void* model, eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices);
    void loadMesh(VulkanContext* context, void* mesh, const void* model, eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices);

private:
    VulkanContext* context = nullptr;
    eastl::vector<Mesh> meshes;
    eastl::vector<Texture> textures;
};

}