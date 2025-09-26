#pragma once

#include <cassert>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/hash_map.h>

#include "renderer/Camera.hpp"
#include "renderer/CameraController.hpp"
#include "renderer/Renderable.hpp"
#include "renderer/Mesh.hpp"
#include "math/AABB.hpp"

namespace violet {

class MaterialInstance;

struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 getMatrix() const {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        return T * R * S;
    }

    void setPosition(const glm::vec3& pos) { position = pos; }
    void setRotation(const glm::quat& rot) { rotation = rot; }
    void setScale(const glm::vec3& s) { scale = s; }

    void translate(const glm::vec3& offset) { position += offset; }
    void rotate(const glm::quat& rot) { rotation = rot * rotation; }
    void setScale(float uniformScale) { scale = glm::vec3(uniformScale); }
};

struct TransformComponent {
    Transform local;        // Local transform relative to parent
    Transform world;        // World transform (computed from hierarchy)
    bool      dirty = true; // Whether world transform needs recomputation

    TransformComponent() = default;
    TransformComponent(const Transform& localTransform) : local(localTransform) {}
};

struct MeshComponent {
    eastl::unique_ptr<Mesh> mesh;
    AABB worldBounds;  // Legacy - will be removed
    eastl::vector<AABB> subMeshWorldBounds;  // World-space bounds for each SubMesh
    bool dirty = true;

    MeshComponent() = default;
    MeshComponent(eastl::unique_ptr<Mesh> meshPtr) : mesh(eastl::move(meshPtr)) {
        if (mesh) {
            worldBounds = mesh->getLocalBounds();
            // Initialize SubMesh world bounds
            subMeshWorldBounds.resize(mesh->getSubMeshCount());
            for (size_t i = 0; i < mesh->getSubMeshCount(); ++i) {
                subMeshWorldBounds[i] = mesh->getSubMesh(i).localBounds;
            }
        }
    }

    void updateWorldBounds(const glm::mat4& worldTransform) {
        if (mesh) {
            // Update legacy bounds
            worldBounds = mesh->getLocalBounds().transform(worldTransform);

            // Update each SubMesh world bounds
            for (size_t i = 0; i < mesh->getSubMeshCount(); ++i) {
                const auto& subMesh = mesh->getSubMesh(i);
                subMeshWorldBounds[i] = subMesh.localBounds.transform(worldTransform);
            }
        }
    }

    const AABB& getSubMeshWorldBounds(size_t index) const {
        assert(index < subMeshWorldBounds.size() && "SubMesh index out of bounds");
        return subMeshWorldBounds[index];
    }

    size_t getSubMeshCount() const {
        return mesh ? mesh->getSubMeshCount() : 0;
    }
};

struct MaterialComponent {
    // Map from SubMesh material index to global material ID
    eastl::hash_map<uint32_t, uint32_t> materialIndexToId;

    MaterialComponent() = default;
    MaterialComponent(const eastl::vector<uint32_t>& materialIds) {
        // Store material IDs with their original GLTF indices
        for (size_t i = 0; i < materialIds.size(); ++i) {
            materialIndexToId[static_cast<uint32_t>(i)] = materialIds[i];
        }
    }

    uint32_t getMaterialId(uint32_t subMeshMaterialIndex) const {
        auto it = materialIndexToId.find(subMeshMaterialIndex);
        return it != materialIndexToId.end() ? it->second : 0;
    }
};

// Renderable has been moved to renderer/Renderable.hpp

struct CameraComponent {
    eastl::unique_ptr<Camera> camera;
    bool                      isActive = false;

    CameraComponent() = default;
    CameraComponent(eastl::unique_ptr<Camera> cam) : camera(eastl::move(cam)) {}

    CameraComponent(CameraComponent&& other) noexcept : camera(eastl::move(other.camera)), isActive(other.isActive) {}

    CameraComponent& operator=(CameraComponent&& other) noexcept {
        if (this != &other) {
            camera   = eastl::move(other.camera);
            isActive = other.isActive;
        }
        return *this;
    }

    CameraComponent(const CameraComponent&)            = delete;
    CameraComponent& operator=(const CameraComponent&) = delete;
};

struct CameraControllerComponent {
    eastl::unique_ptr<CameraController> controller;

    CameraControllerComponent() = default;
    CameraControllerComponent(eastl::unique_ptr<CameraController> ctrl) : controller(eastl::move(ctrl)) {}

    CameraControllerComponent(CameraControllerComponent&& other) noexcept : controller(eastl::move(other.controller)) {}

    CameraControllerComponent& operator=(CameraControllerComponent&& other) noexcept {
        if (this != &other) {
            controller = eastl::move(other.controller);
        }
        return *this;
    }

    CameraControllerComponent(const CameraControllerComponent&)            = delete;
    CameraControllerComponent& operator=(const CameraControllerComponent&) = delete;
};

} // namespace violet
