#include "SceneLoader.hpp"

#include <tiny_gltf.h>

#include <stdexcept>

#include "core/Log.hpp"
#include "renderer/Material.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/Vertex.hpp"
#include "renderer/VulkanContext.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace violet {

eastl::unique_ptr<Scene>
SceneLoader::loadFromGLTF(VulkanContext* context, const eastl::string& filePath, entt::registry* world) {
    auto scene = eastl::make_unique<Scene>();

    tinygltf::Model    gltfModel;
    tinygltf::TinyGLTF loader;
    std::string        err;
    std::string        warn;

    bool ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filePath.c_str());

    if (!warn.empty()) {
        VT_WARN("glTF warning: {}", warn);
    }

    if (!err.empty()) {
        VT_ERROR("glTF error: {}", err);
    }

    if (!ret) {
        throw std::runtime_error("Failed to parse glTF");
    }

    VT_INFO("Loading glTF scene: {}", filePath.c_str());
    VT_INFO(
        "Nodes: {}, Meshes: {}, Materials: {}",
        gltfModel.nodes.size(),
        gltfModel.meshes.size(),
        gltfModel.materials.size()
    );


    const tinygltf::Scene& gltfScene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];

    for (size_t i = 0; i < gltfScene.nodes.size(); i++) {
        loadNode(context, scene.get(), &gltfModel.nodes[gltfScene.nodes[i]], &gltfModel, 0, world);
    }

    VT_INFO("Scene loaded successfully: {} nodes", scene->getNodeCount());

    return scene;
}

void SceneLoader::loadNode(
    VulkanContext*  context,
    Scene*          scene,
    void*           nodePtr,
    const void*     modelPtr,
    uint32_t        parentId,
    entt::registry* world
) {
    tinygltf::Node*        gltfNode  = static_cast<tinygltf::Node*>(nodePtr);
    const tinygltf::Model* gltfModel = static_cast<const tinygltf::Model*>(modelPtr);

    uint32_t nodeId = createNodeFromGLTF(scene, nodePtr, parentId);
    Node*    node   = scene->getNode(nodeId);

    if (!node) {
        VT_ERROR("Failed to create node");
        return;
    }

    // Create ECS entity and associate with node
    entt::entity entity = world->create();
    node->entity        = entity;

    // Add TransformComponent with local transform
    Transform localTransform = extractTransform(nodePtr);
    world->emplace<TransformComponent>(entity, localTransform);

    // Log node position
    VT_INFO("Node '{}' added at position: ({:.2f}, {:.2f}, {:.2f})",
            node->name.c_str(),
            localTransform.position.x,
            localTransform.position.y,
            localTransform.position.z);

    if (gltfNode->mesh > -1) {
        const tinygltf::Mesh& gltfMesh = gltfModel->meshes[gltfNode->mesh];

        eastl::vector<Vertex>   vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<SubMesh>  subMeshes;

        for (size_t i = 0; i < gltfMesh.primitives.size(); i++) {
            const tinygltf::Primitive& primitive   = gltfMesh.primitives[i];
            uint32_t                   indexStart  = static_cast<uint32_t>(indices.size());
            uint32_t                   vertexStart = static_cast<uint32_t>(vertices.size());

            if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
                const tinygltf::Accessor& accessor =
                    gltfModel->accessors[primitive.attributes.find("POSITION")->second];
                const tinygltf::BufferView& bufferView = gltfModel->bufferViews[accessor.bufferView];
                const tinygltf::Buffer&     buffer     = gltfModel->buffers[bufferView.buffer];

                const float* positions =
                    reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

                for (size_t v = 0; v < accessor.count; v++) {
                    Vertex vertex{};
                    vertex.pos      = glm::vec3(positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]);
                    vertex.normal   = glm::vec3(0.0f, 1.0f, 0.0f);
                    vertex.texCoord = glm::vec2(0.0f, 0.0f);
                    vertex.color    = glm::vec3(1.0f);
                    vertex.tangent  = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    vertices.push_back(vertex);
                }
            }

            if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                const tinygltf::Accessor& accessor = gltfModel->accessors[primitive.attributes.find("NORMAL")->second];
                const tinygltf::BufferView& bufferView = gltfModel->bufferViews[accessor.bufferView];
                const tinygltf::Buffer&     buffer     = gltfModel->buffers[bufferView.buffer];

                const float* normals =
                    reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

                for (size_t v = 0; v < accessor.count; v++) {
                    vertices[vertexStart + v].normal =
                        glm::vec3(normals[v * 3], normals[v * 3 + 1], normals[v * 3 + 2]);
                }
            }

            if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                const tinygltf::Accessor& accessor =
                    gltfModel->accessors[primitive.attributes.find("TEXCOORD_0")->second];
                const tinygltf::BufferView& bufferView = gltfModel->bufferViews[accessor.bufferView];
                const tinygltf::Buffer&     buffer     = gltfModel->buffers[bufferView.buffer];

                const float* texCoords =
                    reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

                for (size_t v = 0; v < accessor.count; v++) {
                    vertices[vertexStart + v].texCoord = glm::vec2(texCoords[v * 2], texCoords[v * 2 + 1]);
                }
            }

            if (primitive.indices > -1) {
                const tinygltf::Accessor&   accessor   = gltfModel->accessors[primitive.indices];
                const tinygltf::BufferView& bufferView = gltfModel->bufferViews[accessor.bufferView];
                const tinygltf::Buffer&     buffer     = gltfModel->buffers[bufferView.buffer];

                switch (accessor.componentType) {
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
                        const uint32_t* buf = reinterpret_cast<const uint32_t*>(
                            &buffer.data[accessor.byteOffset + bufferView.byteOffset]
                        );
                        for (size_t index = 0; index < accessor.count; index++) {
                            indices.push_back(buf[index] + vertexStart);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                        const uint16_t* buf = reinterpret_cast<const uint16_t*>(
                            &buffer.data[accessor.byteOffset + bufferView.byteOffset]
                        );
                        for (size_t index = 0; index < accessor.count; index++) {
                            indices.push_back(buf[index] + vertexStart);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                        const uint8_t* buf =
                            reinterpret_cast<const uint8_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
                        for (size_t index = 0; index < accessor.count; index++) {
                            indices.push_back(buf[index] + vertexStart);
                        }
                        break;
                    }
                }
            }

            SubMesh subMesh;
            subMesh.firstIndex    = indexStart;
            subMesh.indexCount    = static_cast<uint32_t>(indices.size()) - indexStart;
            subMesh.materialIndex = primitive.material > -1 ? static_cast<uint32_t>(primitive.material) : 0;
            subMeshes.push_back(subMesh);
        }

        if (!vertices.empty()) {
            // Create mesh and store as ECS component using unique_ptr for RAII
            auto meshPtr = eastl::make_unique<Mesh>();
            meshPtr->create(context, vertices, indices, subMeshes);
            world->emplace<MeshComponent>(entity, eastl::move(meshPtr));

            VT_INFO("Created mesh component for node: {}", gltfNode->name.c_str());

            // Add material component if available
            if (!gltfMesh.primitives.empty() && gltfMesh.primitives[0].material > -1) {
                // Material handling will be implemented when materials are properly loaded
                // For now, we'll leave MaterialComponent empty
            }
        }
    }

    for (size_t i = 0; i < gltfNode->children.size(); i++) {
        loadNode(
            context,
            scene,
            const_cast<tinygltf::Node*>(&gltfModel->nodes[gltfNode->children[i]]),
            modelPtr,
            nodeId,
            world
        );
    }
}

void SceneLoader::loadTextures(Scene* scene, Node* nodePtr) {}

Transform SceneLoader::extractTransform(const void* nodePtr) {
    const tinygltf::Node* gltfNode = static_cast<const tinygltf::Node*>(nodePtr);
    Transform             transform;

    if (gltfNode->matrix.size() == 16) {
        glm::mat4 matrix = glm::mat4(
            gltfNode->matrix[0],
            gltfNode->matrix[1],
            gltfNode->matrix[2],
            gltfNode->matrix[3],
            gltfNode->matrix[4],
            gltfNode->matrix[5],
            gltfNode->matrix[6],
            gltfNode->matrix[7],
            gltfNode->matrix[8],
            gltfNode->matrix[9],
            gltfNode->matrix[10],
            gltfNode->matrix[11],
            gltfNode->matrix[12],
            gltfNode->matrix[13],
            gltfNode->matrix[14],
            gltfNode->matrix[15]
        );

        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(matrix, scale, rotation, translation, skew, perspective);

        transform.position = translation;
        transform.rotation = rotation;
        transform.scale    = scale;
    } else {
        if (gltfNode->translation.size() == 3) {
            transform.position =
                glm::vec3(gltfNode->translation[0], gltfNode->translation[1], gltfNode->translation[2]);
        }

        if (gltfNode->rotation.size() == 4) {
            transform.rotation =
                glm::quat(gltfNode->rotation[3], gltfNode->rotation[0], gltfNode->rotation[1], gltfNode->rotation[2]);
        }

        if (gltfNode->scale.size() == 3) {
            transform.scale = glm::vec3(gltfNode->scale[0], gltfNode->scale[1], gltfNode->scale[2]);
        }
    }

    return transform;
}

uint32_t SceneLoader::createNodeFromGLTF(Scene* scene, void* nodePtr, uint32_t parentId) {
    tinygltf::Node* gltfNode = static_cast<tinygltf::Node*>(nodePtr);

    Node node;
    node.name     = gltfNode->name.empty() ? "Unnamed Node" : eastl::string(gltfNode->name.c_str());
    node.parentId = parentId;
    // entity will be set after node is added to scene

    return scene->addNode(node);
}

} // namespace violet
