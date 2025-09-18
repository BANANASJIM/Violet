#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>
#include <EASTL/array.h>
#include <EASTL/hash_map.h>

namespace violet {

class VulkanContext;
struct Vertex;

}

// Hash function for Vertex to enable using it in EASTL hash_map - must be defined before use
namespace eastl {
    template <>
    struct hash<violet::Vertex> {
        size_t operator()(const violet::Vertex& vertex) const;
    };
}

namespace violet {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 color;
    glm::vec4 tangent{0.0f, 0.0f, 0.0f, 1.0f};

    bool operator==(const Vertex& other) const {
        return pos == other.pos && normal == other.normal &&
               texCoord == other.texCoord && color == other.color &&
               tangent == other.tangent;
    }

    static vk::VertexInputBindingDescription getBindingDescription() {
        vk::VertexInputBindingDescription bindingDescription;
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = vk::VertexInputRate::eVertex;
        return bindingDescription;
    }

    static eastl::array<vk::VertexInputAttributeDescription, 5> getAttributeDescriptions() {
        eastl::array<vk::VertexInputAttributeDescription, 5> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = vk::Format::eR32G32Sfloat;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[3].offset = offsetof(Vertex, color);

        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = vk::Format::eR32G32B32A32Sfloat;
        attributeDescriptions[4].offset = offsetof(Vertex, tangent);

        return attributeDescriptions;
    }
};

class VertexBuffer {
public:
    VertexBuffer() = default;
    ~VertexBuffer() { cleanup(); }

    // Delete copy operations
    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;

    // Enable move operations
    VertexBuffer(VertexBuffer&&) = default;
    VertexBuffer& operator=(VertexBuffer&&) = default;

    void create(VulkanContext* context, const eastl::vector<Vertex>& vertices);
    void create(VulkanContext* context, const eastl::vector<uint32_t>& indices);
    void createWithDeduplication(VulkanContext* context, const eastl::vector<Vertex>& inputVertices);
    void cleanup();

    vk::Buffer getBuffer() const { return buffer; }
    uint32_t getIndexCount() const { return indexCount; }

private:
    VulkanContext* context = nullptr;
    vk::Buffer buffer = nullptr;
    vk::DeviceMemory bufferMemory = nullptr;
    uint32_t indexCount = 0;
};

// Utility class for vertex deduplication
class VertexDeduplicator {
public:
    static void deduplicate(const eastl::vector<Vertex>& inputVertices,
                           eastl::vector<Vertex>& uniqueVertices,
                           eastl::vector<uint32_t>& indices) {
        eastl::hash_map<Vertex, uint32_t> uniqueVertexMap;
        uniqueVertices.clear();
        indices.clear();

        for (const auto& vertex : inputVertices) {
            auto it = uniqueVertexMap.find(vertex);
            if (it == uniqueVertexMap.end()) {
                uint32_t index = static_cast<uint32_t>(uniqueVertices.size());
                uniqueVertexMap[vertex] = index;
                uniqueVertices.push_back(vertex);
                indices.push_back(index);
            } else {
                indices.push_back(it->second);
            }
        }
    }
};

}

// Hash function implementation
inline size_t eastl::hash<violet::Vertex>::operator()(const violet::Vertex& vertex) const {
    size_t h1 = hash<float>()(vertex.pos.x) ^ (hash<float>()(vertex.pos.y) << 1);
    size_t h2 = hash<float>()(vertex.pos.z) ^ (hash<float>()(vertex.normal.x) << 1);
    size_t h3 = hash<float>()(vertex.normal.y) ^ (hash<float>()(vertex.normal.z) << 1);
    size_t h4 = hash<float>()(vertex.texCoord.x) ^ (hash<float>()(vertex.texCoord.y) << 1);
    size_t h5 = hash<float>()(vertex.color.x) ^ (hash<float>()(vertex.color.y) << 1);
    size_t h6 = hash<float>()(vertex.color.z);
    return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5);
}