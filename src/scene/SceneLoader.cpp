#include "SceneLoader.hpp"

#include <tiny_gltf.h>

#include "core/Exception.hpp"
#include <EASTL/algorithm.h>

#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include "renderer/Material.hpp"
#include "renderer/MaterialManager.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/ForwardRenderer.hpp"
#include "renderer/Texture.hpp"
#include "renderer/Vertex.hpp"
#include "renderer/VulkanContext.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace violet {

eastl::unique_ptr<Scene>
SceneLoader::loadFromGLTF(VulkanContext* context, const eastl::string& filePath, entt::registry* world, ForwardRenderer* renderer, Texture* defaultTexture) {
    auto scene = eastl::make_unique<Scene>();

    tinygltf::Model    gltfModel;
    tinygltf::TinyGLTF loader;
    std::string        err;
    std::string        warn;

    bool ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filePath.c_str());

    if (!warn.empty()) {
        violet::Log::warn("Scene", "glTF warning: {}", warn);
    }

    if (!err.empty()) {
        violet::Log::error("Scene", "glTF error: {}", err);
    }

    if (!ret) {
        throw RuntimeError("Failed to parse glTF");
    }

    violet::Log::info("Scene", "Loading glTF scene: {}", filePath.c_str());
    violet::Log::info("Scene",
        "Nodes: {}, Meshes: {}, Materials: {}, Textures: {}, Images: {}",
        gltfModel.nodes.size(),
        gltfModel.meshes.size(),
        gltfModel.materials.size(),
        gltfModel.textures.size(),
        gltfModel.images.size()
    );

    // Create loading context
    GLTFLoadContext loadCtx;
    loadCtx.vulkanContext = context;
    loadCtx.renderer = renderer;
    loadCtx.defaultTexture = defaultTexture;

    loadTextures(loadCtx, &gltfModel);
    loadMaterials(loadCtx, &gltfModel, filePath);

    const tinygltf::Scene& gltfScene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];

    size_t lastSlash = filePath.find_last_of("/\\");
    size_t lastDot = filePath.find_last_of(".");
    eastl::string modelName = filePath.substr(
        lastSlash != eastl::string::npos ? lastSlash + 1 : 0,
        lastDot != eastl::string::npos ? lastDot - (lastSlash != eastl::string::npos ? lastSlash + 1 : 0) : eastl::string::npos
    );

    // Create a parent node for the entire imported model
    uint32_t parentNodeId = 0;
    if (gltfScene.nodes.size() > 1 || !modelName.empty()) {
        // Create parent node only if there are multiple root nodes or we want to group the model
        Node parentNode;
        parentNode.name = modelName.empty() ? "Imported Model" : modelName;
        parentNode.parentId = 0;  // Root level node
        parentNode.entity = entt::null;  // Parent node has no entity/mesh
        parentNodeId = scene->addNode(parentNode);

        violet::Log::info("Scene", "Created parent node '{}' for imported model", parentNode.name.c_str());
    }

    for (size_t i = 0; i < gltfScene.nodes.size(); i++) {
        loadNode(loadCtx, scene.get(), &gltfModel.nodes[gltfScene.nodes[i]], &gltfModel, parentNodeId, world);
    }

    violet::Log::info("Scene", "Scene loaded successfully: {} nodes", scene->getNodeCount());

    // Build BVH for the loaded scene
    renderer->collectRenderables(*world);
    renderer->buildSceneBVH(*world);

    return scene;
}

void SceneLoader::loadNode(
    GLTFLoadContext& loadCtx,
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
        violet::Log::error("Scene", "Failed to create node");
        return;
    }

    // Create ECS entity and associate with node
    entt::entity entity = world->create();
    node->entity        = entity;

    // Add TransformComponent with local transform
    Transform localTransform = extractTransform(nodePtr);
    world->emplace<TransformComponent>(entity, localTransform);


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
            meshPtr->create(loadCtx.vulkanContext, vertices, indices, subMeshes);
            world->emplace<MeshComponent>(entity, eastl::move(meshPtr));

            // NOTE: World bounds will be updated after world transforms are computed
            // Don't update bounds here since world transform hasn't been calculated yet


            // Create material ID mapping for this mesh
            eastl::vector<uint32_t> meshMaterialIds;
            for (const auto& subMesh : subMeshes) {
                uint32_t gltfMatIndex = subMesh.materialIndex;
                if (gltfMatIndex < loadCtx.materialIds.size()) {
                    meshMaterialIds.push_back(loadCtx.materialIds[gltfMatIndex]);
                } else {
                    // Use first material ID as fallback
                    meshMaterialIds.push_back(!loadCtx.materialIds.empty() ? loadCtx.materialIds[0] : 0);
                }
            }

            if (!meshMaterialIds.empty()) {
                MaterialComponent matComp;
                // Store the mapping from SubMesh material index to global material ID
                for (const auto& subMesh : subMeshes) {
                    uint32_t gltfMatIndex = subMesh.materialIndex;
                    if (gltfMatIndex < loadCtx.materialIds.size()) {
                        matComp.materialIndexToId[gltfMatIndex] = loadCtx.materialIds[gltfMatIndex];
                    }
                }
                world->emplace<MaterialComponent>(entity, eastl::move(matComp));
            }
        }
    }

    for (size_t i = 0; i < gltfNode->children.size(); i++) {
        loadNode(
            loadCtx,
            scene,
            const_cast<tinygltf::Node*>(&gltfModel->nodes[gltfNode->children[i]]),
            modelPtr,
            nodeId,
            world
        );
    }
}

void SceneLoader::loadTextures(GLTFLoadContext& loadCtx, const void* modelPtr) {
    const tinygltf::Model* gltfModel = static_cast<const tinygltf::Model*>(modelPtr);

    loadCtx.textures.resize(gltfModel->textures.size());

    for (size_t i = 0; i < gltfModel->textures.size(); i++) {
        const tinygltf::Texture& gltfTexture = gltfModel->textures[i];

        if (gltfTexture.source >= 0 && gltfTexture.source < gltfModel->images.size()) {
            const tinygltf::Image& gltfImage = gltfModel->images[gltfTexture.source];

            auto texture = eastl::make_unique<Texture>();

            if (!gltfImage.image.empty()) {
                // Load from embedded image data - format determined later based on usage
                texture->loadFromMemory(
                    loadCtx.vulkanContext,
                    gltfImage.image.data(),
                    gltfImage.image.size(),
                    gltfImage.width,
                    gltfImage.height,
                    gltfImage.component,
                    true  // Default to sRGB, will be corrected later
                );
            } else if (!gltfImage.uri.empty()) {
                // Load from external file
                texture->loadFromFile(loadCtx.vulkanContext, eastl::string(gltfImage.uri.c_str()));
            }

            // Set sampler from DescriptorManager
            texture->setSampler(loadCtx.renderer->getDescriptorManager().getSampler(SamplerType::Default));

            // Transfer ownership to Renderer for persistent storage
            Texture* texturePtr = loadCtx.renderer->getMaterialManager()->addTexture(eastl::move(texture));
            loadCtx.textures[i] = texturePtr;
        }
    }
}

void SceneLoader::loadMaterials(GLTFLoadContext& loadCtx, const void* modelPtr, const eastl::string& filePath) {
    const tinygltf::Model* gltfModel = static_cast<const tinygltf::Model*>(modelPtr);

    loadCtx.materials.resize(gltfModel->materials.size());

    // Get shared PBR bindless material (all instances share this)
    Material* pbrMaterial = loadCtx.renderer->getPBRBindlessMaterial();
    if (!pbrMaterial) {
        violet::Log::error("Scene", "PBR bindless material not initialized");
        return;
    }

    for (size_t i = 0; i < gltfModel->materials.size(); i++) {
        const tinygltf::Material& gltfMaterial = gltfModel->materials[i];

        // Create material instance using MaterialManager
        MaterialInstanceDesc instanceDesc{
            .material = pbrMaterial,
            .type = MaterialType::PBR,
            .name = gltfMaterial.name.empty() ? "Material" : eastl::string(gltfMaterial.name.c_str())
        };
        uint32_t instanceId = loadCtx.renderer->getMaterialManager()->createMaterialInstance(instanceDesc);
        PBRMaterialInstance* instance = static_cast<PBRMaterialInstance*>(
            loadCtx.renderer->getMaterialManager()->getMaterialInstance(instanceId)
        );

        // Extract PBR material data
        PBRMaterialData& data = instance->getData();

        // Base color
        if (gltfMaterial.values.find("baseColorFactor") != gltfMaterial.values.end()) {
            const auto& factor = gltfMaterial.values.at("baseColorFactor").ColorFactor();
            data.baseColorFactor = glm::vec4(factor[0], factor[1], factor[2], factor[3]);
        }

        // Metallic and roughness
        if (gltfMaterial.values.find("metallicFactor") != gltfMaterial.values.end()) {
            data.metallicFactor = static_cast<float>(gltfMaterial.values.at("metallicFactor").Factor());
        }
        if (gltfMaterial.values.find("roughnessFactor") != gltfMaterial.values.end()) {
            data.roughnessFactor = static_cast<float>(gltfMaterial.values.at("roughnessFactor").Factor());
        }

        // Normal scale
        if (gltfMaterial.normalTexture.scale != 0) {
            data.normalScale = static_cast<float>(gltfMaterial.normalTexture.scale);
        }

        // Occlusion strength
        if (gltfMaterial.occlusionTexture.strength != 0) {
            data.occlusionStrength = static_cast<float>(gltfMaterial.occlusionTexture.strength);
        }

        // Emissive factor
        if (gltfMaterial.emissiveFactor.size() == 3) {
            data.emissiveFactor = glm::vec3(
                gltfMaterial.emissiveFactor[0],
                gltfMaterial.emissiveFactor[1],
                gltfMaterial.emissiveFactor[2]
            );
        }

        // Alpha cutoff
        if (gltfMaterial.alphaCutoff != 0) {
            data.alphaCutoff = static_cast<float>(gltfMaterial.alphaCutoff);
        }

        // Set alpha mode on shared material
        if (gltfMaterial.alphaMode == "OPAQUE") {
            pbrMaterial->setAlphaMode(Material::AlphaMode::Opaque);
        } else if (gltfMaterial.alphaMode == "MASK") {
            pbrMaterial->setAlphaMode(Material::AlphaMode::Mask);
        } else if (gltfMaterial.alphaMode == "BLEND") {
            pbrMaterial->setAlphaMode(Material::AlphaMode::Blend);
        }

        // Double sided
        pbrMaterial->setDoubleSided(gltfMaterial.doubleSided);

        // Assign textures
        // Base color texture
        if (gltfMaterial.values.find("baseColorTexture") != gltfMaterial.values.end()) {
            int texIndex = gltfMaterial.values.at("baseColorTexture").TextureIndex();
            if (texIndex >= 0 && texIndex < loadCtx.textures.size() && loadCtx.textures[texIndex]) {
                instance->setBaseColorTexture(loadCtx.textures[texIndex]);
            } else {
                instance->setBaseColorTexture(loadCtx.defaultTexture);
                violet::Log::warn("Scene", "Material {} using default baseColor texture (invalid index {})", i, texIndex);
            }
        } else {
            instance->setBaseColorTexture(loadCtx.defaultTexture);
        }

        // Metallic-roughness texture
        if (gltfMaterial.values.find("metallicRoughnessTexture") != gltfMaterial.values.end()) {
            int texIndex = gltfMaterial.values.at("metallicRoughnessTexture").TextureIndex();
            if (texIndex >= 0 && texIndex < loadCtx.textures.size() && loadCtx.textures[texIndex]) {
                instance->setMetallicRoughnessTexture(loadCtx.textures[texIndex]);
            } else {
                instance->setMetallicRoughnessTexture(loadCtx.renderer->getMaterialManager()->getDefaultTexture(DefaultTextureType::MetallicRoughness));
                violet::Log::warn("Scene", "Material {} using default metallicRoughness texture (invalid index {})", i, texIndex);
            }
        } else {
            instance->setMetallicRoughnessTexture(loadCtx.renderer->getMaterialManager()->getDefaultTexture(DefaultTextureType::MetallicRoughness));
        }

        // Normal texture
        if (gltfMaterial.normalTexture.index >= 0) {
            int texIndex = gltfMaterial.normalTexture.index;
            if (texIndex < loadCtx.textures.size() && loadCtx.textures[texIndex]) {
                instance->setNormalTexture(loadCtx.textures[texIndex]);
            } else {
                instance->setNormalTexture(loadCtx.renderer->getMaterialManager()->getDefaultTexture(DefaultTextureType::Normal));
                violet::Log::warn("Scene", "Material {} using default normal texture (invalid index {})", i, texIndex);
            }
        } else {
            instance->setNormalTexture(loadCtx.renderer->getMaterialManager()->getDefaultTexture(DefaultTextureType::Normal));
        }

        // Occlusion texture
        if (gltfMaterial.occlusionTexture.index >= 0) {
            int texIndex = gltfMaterial.occlusionTexture.index;
            if (texIndex < loadCtx.textures.size() && loadCtx.textures[texIndex]) {
                instance->setOcclusionTexture(loadCtx.textures[texIndex]);
            } else {
                instance->setOcclusionTexture(loadCtx.defaultTexture);
            }
        } else {
            instance->setOcclusionTexture(loadCtx.defaultTexture);
        }

        // Emissive texture
        if (gltfMaterial.emissiveTexture.index >= 0) {
            int texIndex = gltfMaterial.emissiveTexture.index;
            if (texIndex < loadCtx.textures.size() && loadCtx.textures[texIndex]) {
                instance->setEmissiveTexture(loadCtx.textures[texIndex]);
            } else {
                instance->setEmissiveTexture(loadCtx.defaultTexture);
            }
        } else {
            instance->setEmissiveTexture(loadCtx.defaultTexture);
        }

        // Update material data in SSBO (bindless architecture)
        // All textures have been registered in bindless array, now sync material parameters
        instance->updateMaterialData();

        // Generate unique material ID (combine file hash with material index for uniqueness)
        // For now, use a simple scheme: high bits for file, low bits for material index
        static uint32_t fileIdCounter = 0;
        static eastl::hash_map<eastl::string, uint32_t> fileIdMap;

        uint32_t fileId = 0;
        auto it = fileIdMap.find(filePath);
        if (it == fileIdMap.end()) {
            fileId = ++fileIdCounter;
            fileIdMap[filePath] = fileId;
        } else {
            fileId = it->second;
        }

        // Create unique material ID: fileId << 16 | materialIndex
        uint32_t materialId = (fileId << 16) | static_cast<uint32_t>(i);

        // Register material instance globally using MaterialManager
        loadCtx.renderer->getMaterialManager()->registerGlobalMaterial(materialId, instanceId);

        loadCtx.materials[i] = instance;
        loadCtx.materialIds.push_back(materialId);
    }
}

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

    // Check for very small scale values and normalize them to 1.0
    if (transform.scale.x < 0.1f && transform.scale.x > 0.0f) {
        float originalScale = transform.scale.x;
        float scaleFactor = 1.0f / originalScale;
        transform.scale = glm::vec3(1.0f);
        violet::Log::info("Scene", "Normalized tiny scale ({:.3f}) to 1.0, vertices will appear {:.0f}x larger",
                originalScale, scaleFactor);
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
