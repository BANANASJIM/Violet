#include "Model.hpp"
#include "VulkanContext.hpp"
#include <tiny_gltf.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace violet {

void Model::loadFromGLTF(VulkanContext* ctx, const eastl::string& filePath) {
    context = ctx;

    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filePath.c_str());

    if (!warn.empty()) {
        spdlog::warn("glTF warning: {}", warn);
    }

    if (!err.empty()) {
        spdlog::error("glTF error: {}", err);
    }

    if (!ret) {
        throw std::runtime_error("Failed to parse glTF");
    }

    // Load textures
    textures.resize(gltfModel.textures.size());
    for (size_t i = 0; i < gltfModel.textures.size(); i++) {
        tinygltf::Texture& tex = gltfModel.textures[i];
        if (tex.source > -1) {
            tinygltf::Image& image = gltfModel.images[tex.source];

            // Create a temporary texture from image data
            // For now, just create empty textures - full implementation would load from image data
            spdlog::info("Loading texture: {}", image.name);
        }
    }

    // Process nodes
    eastl::vector<Vertex> vertices;
    eastl::vector<uint32_t> indices;

    const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
    for (size_t i = 0; i < scene.nodes.size(); i++) {
        loadNode(ctx, &gltfModel.nodes[scene.nodes[i]], &gltfModel, vertices, indices);
    }

    if (!vertices.empty()) {
        Mesh mesh;
        mesh.vertexBuffer.create(ctx, vertices);
        mesh.indexBuffer.create(ctx, indices);
        mesh.indexCount = static_cast<uint32_t>(indices.size());
        meshes.push_back(mesh);
    }
}

void Model::cleanup() {
    for (auto& mesh : meshes) {
        mesh.vertexBuffer.cleanup();
        mesh.indexBuffer.cleanup();
    }
    for (auto& texture : textures) {
        texture.cleanup();
    }
    meshes.clear();
    textures.clear();
}

void Model::loadNode(VulkanContext* ctx, void* nodePtr, const void* modelPtr, eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices) {
    tinygltf::Node* node = static_cast<tinygltf::Node*>(nodePtr);
    const tinygltf::Model* model = static_cast<const tinygltf::Model*>(modelPtr);

    // Process mesh if present
    if (node->mesh > -1) {
        loadMesh(ctx, const_cast<tinygltf::Mesh*>(&model->meshes[node->mesh]), modelPtr, vertices, indices);
    }

    // Process children
    for (size_t i = 0; i < node->children.size(); i++) {
        loadNode(ctx, const_cast<tinygltf::Node*>(&model->nodes[node->children[i]]), modelPtr, vertices, indices);
    }
}

void Model::loadMesh(VulkanContext* ctx, void* meshPtr, const void* modelPtr, eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices) {
    tinygltf::Mesh* mesh = static_cast<tinygltf::Mesh*>(meshPtr);
    const tinygltf::Model* model = static_cast<const tinygltf::Model*>(modelPtr);

    for (size_t i = 0; i < mesh->primitives.size(); i++) {
        const tinygltf::Primitive& primitive = mesh->primitives[i];
        uint32_t indexStart = static_cast<uint32_t>(indices.size());
        uint32_t vertexStart = static_cast<uint32_t>(vertices.size());

        // Load vertex positions
        if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
            const tinygltf::Accessor& accessor = model->accessors[primitive.attributes.find("POSITION")->second];
            const tinygltf::BufferView& bufferView = model->bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = model->buffers[bufferView.buffer];

            const float* positions = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

            for (size_t v = 0; v < accessor.count; v++) {
                Vertex vertex{};
                vertex.pos = glm::vec3(positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]);
                vertex.color = glm::vec3(1.0f);
                vertices.push_back(vertex);
            }
        }

        // Load normals if available
        if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
            const tinygltf::Accessor& accessor = model->accessors[primitive.attributes.find("NORMAL")->second];
            const tinygltf::BufferView& bufferView = model->bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = model->buffers[bufferView.buffer];

            const float* normals = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

            for (size_t v = 0; v < accessor.count; v++) {
                vertices[vertexStart + v].normal = glm::vec3(normals[v * 3], normals[v * 3 + 1], normals[v * 3 + 2]);
            }
        }

        // Load texture coordinates if available
        if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
            const tinygltf::Accessor& accessor = model->accessors[primitive.attributes.find("TEXCOORD_0")->second];
            const tinygltf::BufferView& bufferView = model->bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = model->buffers[bufferView.buffer];

            const float* texCoords = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

            for (size_t v = 0; v < accessor.count; v++) {
                vertices[vertexStart + v].texCoord = glm::vec2(texCoords[v * 2], texCoords[v * 2 + 1]);
            }
        }

        // Load indices
        if (primitive.indices > -1) {
            const tinygltf::Accessor& accessor = model->accessors[primitive.indices];
            const tinygltf::BufferView& bufferView = model->bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = model->buffers[bufferView.buffer];

            switch (accessor.componentType) {
                case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
                    const uint32_t* buf = reinterpret_cast<const uint32_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
                    for (size_t index = 0; index < accessor.count; index++) {
                        indices.push_back(buf[index] + vertexStart);
                    }
                    break;
                }
                case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                    const uint16_t* buf = reinterpret_cast<const uint16_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
                    for (size_t index = 0; index < accessor.count; index++) {
                        indices.push_back(buf[index] + vertexStart);
                    }
                    break;
                }
                case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                    const uint8_t* buf = reinterpret_cast<const uint8_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
                    for (size_t index = 0; index < accessor.count; index++) {
                        indices.push_back(buf[index] + vertexStart);
                    }
                    break;
                }
            }
        }
    }
}

}