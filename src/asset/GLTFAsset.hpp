#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "resource/Vertex.hpp"
#include "ecs/Components.hpp"

namespace violet {

struct SubMesh;

// Intermediate glTF asset representation (no GPU resources)
struct GLTFAsset {
    // Mesh data
    struct MeshData {
        eastl::vector<Vertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<SubMesh> submeshes;
    };

    // Texture data
    struct TextureData {
        eastl::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        int channels = 0;
        eastl::string uri;  // Empty if embedded
        bool isEmbedded = true;
    };

    // Material data
    struct MaterialData {
        glm::vec4 baseColorFactor = glm::vec4(1.0f);
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        glm::vec3 emissiveFactor = glm::vec3(0.0f);
        float alphaCutoff = 0.5f;

        int baseColorTexIndex = -1;
        int normalTexIndex = -1;
        int metallicRoughnessTexIndex = -1;
        int occlusionTexIndex = -1;
        int emissiveTexIndex = -1;

        eastl::string name;
        eastl::string alphaMode = "OPAQUE";  // OPAQUE, MASK, BLEND
        bool doubleSided = false;
    };

    // Node data
    struct NodeData {
        eastl::string name;
        Transform transform;
        eastl::vector<uint32_t> children;
        int meshIndex = -1;
        uint32_t parentIndex = 0;
    };

    eastl::vector<MeshData> meshes;
    eastl::vector<TextureData> textures;
    eastl::vector<MaterialData> materials;
    eastl::vector<NodeData> nodes;
    eastl::vector<uint32_t> rootNodes;
};

} // namespace violet
