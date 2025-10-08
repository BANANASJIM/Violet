#include "AssetLoader.hpp"
#include "core/Log.hpp"
#include "core/Exception.hpp"
#include "resource/Mesh.hpp"
#include "resource/ResourceManager.hpp"

#include <EASTL/shared_ptr.h>
#include <tiny_gltf.h>
#include <EASTL/algorithm.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace violet {

eastl::unique_ptr<GLTFAsset> AssetLoader::loadGLTF(const eastl::string& filePath) {
    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filePath.c_str());

    if (!warn.empty()) {
        violet::Log::warn("AssetLoader", "glTF warning: {}", warn);
    }

    if (!err.empty()) {
        violet::Log::error("AssetLoader", "glTF error: {}", err);
    }

    if (!ret) {
        throw RuntimeError("Failed to parse glTF file");
    }

    violet::Log::info("AssetLoader", "Loading glTF: {}", filePath.c_str());
    violet::Log::info("AssetLoader",
        "Nodes: {}, Meshes: {}, Materials: {}, Textures: {}",
        gltfModel.nodes.size(),
        gltfModel.meshes.size(),
        gltfModel.materials.size(),
        gltfModel.textures.size()
    );

    auto asset = eastl::make_unique<GLTFAsset>();

    loadTextures(&gltfModel, asset.get());
    loadMaterials(&gltfModel, asset.get());
    loadMeshes(&gltfModel, asset.get());
    loadNodes(&gltfModel, asset.get());

    return asset;
}

void AssetLoader::loadGLTFAsync(
    const eastl::string& filePath,
    ResourceManager* resourceManager,
    eastl::function<void(eastl::unique_ptr<GLTFAsset>, eastl::string)> callback
) {
    // Shared data for passing between threads
    auto assetPtr = eastl::make_shared<eastl::unique_ptr<GLTFAsset>>();
    auto errorMsg = eastl::make_shared<eastl::string>();

    auto task = eastl::make_shared<AsyncLoadTask>(
        // CPU work: file IO + parsing (runs on worker thread)
        [filePath, assetPtr, errorMsg]() {
            try {
                *assetPtr = loadGLTF(filePath);
            } catch (const Exception& e) {
                *errorMsg = e.what_c_str();
            } catch (const std::exception& e) {
                *errorMsg = eastl::string(e.what());
            } catch (...) {
                *errorMsg = "Unknown error loading glTF";
            }
        },
        // Main thread work: callback with result
        [assetPtr, errorMsg, callback]() {
            callback(eastl::move(*assetPtr), *errorMsg);
        }
    );

    resourceManager->submitAsyncTask(task);
}

void AssetLoader::loadMeshes(void* modelPtr, GLTFAsset* asset) {
    const tinygltf::Model* model = static_cast<const tinygltf::Model*>(modelPtr);
    asset->meshes.resize(model->meshes.size());

    for (size_t meshIdx = 0; meshIdx < model->meshes.size(); meshIdx++) {
        const tinygltf::Mesh& gltfMesh = model->meshes[meshIdx];
        GLTFAsset::MeshData& meshData = asset->meshes[meshIdx];

        for (const auto& primitive : gltfMesh.primitives) {
            uint32_t indexStart = static_cast<uint32_t>(meshData.indices.size());
            uint32_t vertexStart = static_cast<uint32_t>(meshData.vertices.size());

            // Load positions
            if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
                const tinygltf::Accessor& accessor = model->accessors[primitive.attributes.find("POSITION")->second];
                const tinygltf::BufferView& bufferView = model->bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model->buffers[bufferView.buffer];

                const float* positions = reinterpret_cast<const float*>(
                    &buffer.data[bufferView.byteOffset + accessor.byteOffset]
                );

                for (size_t v = 0; v < accessor.count; v++) {
                    Vertex vertex{};
                    vertex.pos = glm::vec3(positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]);
                    vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    vertex.texCoord = glm::vec2(0.0f);
                    vertex.color = glm::vec3(1.0f);
                    vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    meshData.vertices.push_back(vertex);
                }
            }

            // Load normals
            if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                const tinygltf::Accessor& accessor = model->accessors[primitive.attributes.find("NORMAL")->second];
                const tinygltf::BufferView& bufferView = model->bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model->buffers[bufferView.buffer];

                const float* normals = reinterpret_cast<const float*>(
                    &buffer.data[bufferView.byteOffset + accessor.byteOffset]
                );

                for (size_t v = 0; v < accessor.count; v++) {
                    meshData.vertices[vertexStart + v].normal = glm::vec3(
                        normals[v * 3], normals[v * 3 + 1], normals[v * 3 + 2]
                    );
                }
            }

            // Load texture coordinates
            if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                const tinygltf::Accessor& accessor = model->accessors[primitive.attributes.find("TEXCOORD_0")->second];
                const tinygltf::BufferView& bufferView = model->bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model->buffers[bufferView.buffer];

                const float* texCoords = reinterpret_cast<const float*>(
                    &buffer.data[bufferView.byteOffset + accessor.byteOffset]
                );

                for (size_t v = 0; v < accessor.count; v++) {
                    float originalU = texCoords[v * 2];
                    float originalV = texCoords[v * 2 + 1];
                    //  standard Vulkan Y-flip
                    meshData.vertices[vertexStart + v].texCoord = glm::vec2(
                        originalU,
                        1.0f - originalV
                    );
                }
            }

            // Load tangents (required for normal mapping and PBR lighting)
            // glTF tangent is vec4: xyz=tangent direction, w=bitangent handedness (+1 or -1)
            if (primitive.attributes.find("TANGENT") != primitive.attributes.end()) {
                const tinygltf::Accessor& accessor = model->accessors[primitive.attributes.find("TANGENT")->second];
                const tinygltf::BufferView& bufferView = model->bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model->buffers[bufferView.buffer];

                const float* tangents = reinterpret_cast<const float*>(
                    &buffer.data[bufferView.byteOffset + accessor.byteOffset]
                );

                for (size_t v = 0; v < accessor.count; v++) {
                    meshData.vertices[vertexStart + v].tangent = glm::vec4(
                        tangents[v * 4],
                        tangents[v * 4 + 1],
                        tangents[v * 4 + 2],
                        tangents[v * 4 + 3]  // w component: bitangent handedness
                    );
                }
            }
            // Note: If TANGENT is missing, default (1,0,0,1) is used from vertex initialization
            // Future: Generate tangents using MikkTSpace algorithm when missing

            // Load indices
            if (primitive.indices > -1) {
                const tinygltf::Accessor& accessor = model->accessors[primitive.indices];
                const tinygltf::BufferView& bufferView = model->bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model->buffers[bufferView.buffer];

                switch (accessor.componentType) {
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
                        const uint32_t* buf = reinterpret_cast<const uint32_t*>(
                            &buffer.data[accessor.byteOffset + bufferView.byteOffset]
                        );
                        for (size_t i = 0; i < accessor.count; i++) {
                            meshData.indices.push_back(buf[i] + vertexStart);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                        const uint16_t* buf = reinterpret_cast<const uint16_t*>(
                            &buffer.data[accessor.byteOffset + bufferView.byteOffset]
                        );
                        for (size_t i = 0; i < accessor.count; i++) {
                            meshData.indices.push_back(buf[i] + vertexStart);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                        const uint8_t* buf = reinterpret_cast<const uint8_t*>(
                            &buffer.data[accessor.byteOffset + bufferView.byteOffset]
                        );
                        for (size_t i = 0; i < accessor.count; i++) {
                            meshData.indices.push_back(buf[i] + vertexStart);
                        }
                        break;
                    }
                }

                // Create submesh only if indices were loaded
                SubMesh subMesh;
                subMesh.firstIndex = indexStart;
                subMesh.indexCount = static_cast<uint32_t>(meshData.indices.size()) - indexStart;
                subMesh.materialIndex = primitive.material > -1 ? static_cast<uint32_t>(primitive.material) : 0;
                meshData.submeshes.push_back(subMesh);
            } else {
                // Skip primitives without indices (e.g., point clouds, lines)
                violet::Log::warn("AssetLoader", "Skipping primitive without indices in mesh {}", meshIdx);
            }
        }
    }
}

void AssetLoader::loadTextures(void* modelPtr, GLTFAsset* asset) {
    const tinygltf::Model* model = static_cast<const tinygltf::Model*>(modelPtr);
    asset->textures.resize(model->textures.size());

    for (size_t i = 0; i < model->textures.size(); i++) {
        const tinygltf::Texture& gltfTexture = model->textures[i];
        GLTFAsset::TextureData& texData = asset->textures[i];

        if (gltfTexture.source >= 0 && gltfTexture.source < model->images.size()) {
            const tinygltf::Image& gltfImage = model->images[gltfTexture.source];

            texData.width = gltfImage.width;
            texData.height = gltfImage.height;
            texData.channels = gltfImage.component;

            if (!gltfImage.image.empty()) {
                // Embedded image - manual copy due to EASTL/std incompatibility
                texData.pixels.resize(gltfImage.image.size());
                eastl::copy_n(gltfImage.image.data(), gltfImage.image.size(), texData.pixels.data());
                texData.isEmbedded = true;
            } else if (!gltfImage.uri.empty()) {
                // External file reference
                texData.uri = eastl::string(gltfImage.uri.c_str());
                texData.isEmbedded = false;
            }
        }
    }
}

void AssetLoader::loadMaterials(void* modelPtr, GLTFAsset* asset) {
    const tinygltf::Model* model = static_cast<const tinygltf::Model*>(modelPtr);
    asset->materials.resize(model->materials.size());

    for (size_t i = 0; i < model->materials.size(); i++) {
        const tinygltf::Material& gltfMat = model->materials[i];
        GLTFAsset::MaterialData& matData = asset->materials[i];

        matData.name = eastl::string(gltfMat.name.c_str());

        // PBR Metallic Roughness properties
        const auto& pbr = gltfMat.pbrMetallicRoughness;

        // Base color factor
        if (pbr.baseColorFactor.size() == 4) {
            matData.baseColorFactor = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3])
            );
        }

        // Metallic/roughness factors
        matData.metallicFactor = static_cast<float>(pbr.metallicFactor);
        matData.roughnessFactor = static_cast<float>(pbr.roughnessFactor);

        // Normal scale
        if (gltfMat.normalTexture.scale != 0) {
            matData.normalScale = static_cast<float>(gltfMat.normalTexture.scale);
        }

        // Occlusion
        if (gltfMat.occlusionTexture.strength != 0) {
            matData.occlusionStrength = static_cast<float>(gltfMat.occlusionTexture.strength);
        }

        // Emissive
        if (gltfMat.emissiveFactor.size() == 3) {
            matData.emissiveFactor = glm::vec3(
                gltfMat.emissiveFactor[0],
                gltfMat.emissiveFactor[1],
                gltfMat.emissiveFactor[2]
            );
        }

        // Alpha cutoff
        if (gltfMat.alphaCutoff != 0) {
            matData.alphaCutoff = static_cast<float>(gltfMat.alphaCutoff);
        }

        // Alpha mode
        matData.alphaMode = eastl::string(gltfMat.alphaMode.c_str());
        matData.doubleSided = gltfMat.doubleSided;

        // Texture indices from PBR properties
        matData.baseColorTexIndex = pbr.baseColorTexture.index;
        matData.metallicRoughnessTexIndex = pbr.metallicRoughnessTexture.index;
        if (gltfMat.normalTexture.index >= 0) {
            matData.normalTexIndex = gltfMat.normalTexture.index;
        }
        if (gltfMat.occlusionTexture.index >= 0) {
            matData.occlusionTexIndex = gltfMat.occlusionTexture.index;
        }
        if (gltfMat.emissiveTexture.index >= 0) {
            matData.emissiveTexIndex = gltfMat.emissiveTexture.index;
        }
    }
}

void AssetLoader::loadNodes(void* modelPtr, GLTFAsset* asset) {
    const tinygltf::Model* model = static_cast<const tinygltf::Model*>(modelPtr);
    asset->nodes.resize(model->nodes.size());

    // Load all nodes
    for (size_t i = 0; i < model->nodes.size(); i++) {
        const tinygltf::Node& gltfNode = model->nodes[i];
        GLTFAsset::NodeData& nodeData = asset->nodes[i];

        nodeData.name = gltfNode.name.empty() ? "Unnamed Node" : eastl::string(gltfNode.name.c_str());
        nodeData.transform = extractTransform(&gltfNode);
        nodeData.meshIndex = gltfNode.mesh;

        // Convert children indices
        for (int childIdx : gltfNode.children) {
            nodeData.children.push_back(static_cast<uint32_t>(childIdx));
        }
    }

    // Get root nodes from default scene
    const tinygltf::Scene& gltfScene = model->scenes[model->defaultScene > -1 ? model->defaultScene : 0];
    for (int rootIdx : gltfScene.nodes) {
        asset->rootNodes.push_back(static_cast<uint32_t>(rootIdx));
    }
}

Transform AssetLoader::extractTransform(const void* nodePtr) {
    const tinygltf::Node* gltfNode = static_cast<const tinygltf::Node*>(nodePtr);
    Transform transform;

    if (gltfNode->matrix.size() == 16) {
        glm::mat4 matrix = glm::mat4(
            gltfNode->matrix[0], gltfNode->matrix[1], gltfNode->matrix[2], gltfNode->matrix[3],
            gltfNode->matrix[4], gltfNode->matrix[5], gltfNode->matrix[6], gltfNode->matrix[7],
            gltfNode->matrix[8], gltfNode->matrix[9], gltfNode->matrix[10], gltfNode->matrix[11],
            gltfNode->matrix[12], gltfNode->matrix[13], gltfNode->matrix[14], gltfNode->matrix[15]
        );

        glm::vec3 scale, translation, skew;
        glm::quat rotation;
        glm::vec4 perspective;
        glm::decompose(matrix, scale, rotation, translation, skew, perspective);

        transform.position = translation;
        transform.rotation = rotation;
        transform.scale = scale;
    } else {
        if (gltfNode->translation.size() == 3) {
            transform.position = glm::vec3(
                gltfNode->translation[0], gltfNode->translation[1], gltfNode->translation[2]
            );
        }

        if (gltfNode->rotation.size() == 4) {
            transform.rotation = glm::quat(
                gltfNode->rotation[3], gltfNode->rotation[0], gltfNode->rotation[1], gltfNode->rotation[2]
            );
        }

        if (gltfNode->scale.size() == 3) {
            transform.scale = glm::vec3(
                gltfNode->scale[0], gltfNode->scale[1], gltfNode->scale[2]
            );
        }
    }

    // Normalize tiny scales
    if (transform.scale.x < 0.1f && transform.scale.x > 0.0f) {
        float originalScale = transform.scale.x;
        float scaleFactor = 1.0f / originalScale;
        transform.scale = glm::vec3(1.0f);
        violet::Log::info("AssetLoader", "Normalized tiny scale ({:.3f}) to 1.0, vertices will appear {:.0f}x larger",
                originalScale, scaleFactor);
    }

    return transform;
}

} // namespace violet
