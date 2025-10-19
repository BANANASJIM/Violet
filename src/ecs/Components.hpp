#pragma once

#include <cassert>
#include <cfloat>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/hash_map.h>

#include "renderer/camera/Camera.hpp"
#include "input/CameraController.hpp"
#include "renderer/Renderable.hpp"
#include "resource/Mesh.hpp"
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
    eastl::vector<AABB> subMeshWorldBounds;  // World-space bounds for each SubMesh
    bool dirty = true;

    MeshComponent() = default;
    MeshComponent(eastl::unique_ptr<Mesh> meshPtr) : mesh(eastl::move(meshPtr)) {
        if (mesh) {
            // Initialize SubMesh world bounds
            subMeshWorldBounds.resize(mesh->getSubMeshCount());
            for (size_t i = 0; i < mesh->getSubMeshCount(); ++i) {
                subMeshWorldBounds[i] = mesh->getSubMesh(i).localBounds;
            }
        }
    }

    void updateWorldBounds(const glm::mat4& worldTransform) {
        if (mesh) {
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

enum class LightType : uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2  // Reserved for future spotlight implementation
};

struct LightComponent {
    LightType type = LightType::Directional;
    glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);

    // Physical light units:
    // - DirectionalLight: illuminance in lux (lm/m²)
    //   Examples: Direct sunlight ~100,000 lux, overcast day ~10,000 lux,
    //             office lighting ~500 lux, full moon ~0.25 lux
    // - PointLight: luminous power in lumens (lm)
    //   Examples: 100W incandescent ~1600 lm, 60W incandescent ~800 lm,
    //             candle ~12.5 lm, LED bulb (60W equiv) ~800 lm
    float intensity = 1.0f;

    // For directional light (normalized direction vector)
    glm::vec3 direction = glm::vec3(-0.3f, -1.0f, -0.3f);

    // For point/spot light - influence radius for windowing function and culling
    // Beyond this radius, light contribution smoothly falls to zero
    float radius = 100.0f;

    // For spot light (future)
    float innerCutoff = 0.0f;
    float outerCutoff = 0.0f;

    bool enabled = true;

    // Shadow parameters
    bool castsShadows = true;                    // Whether this light casts shadows
    uint32_t shadowResolution = 2048;            // Shadow map resolution (2048 for directional, 512 for point lights)
    float shadowBias = 0.0005f;                  // Depth bias to prevent shadow acne
    float shadowNormalBias = 0.001f;             // Normal-based bias
    float shadowNearPlane = 0.1f;                // Near plane for shadow projection
    float shadowFarPlane = 100.0f;               // Far plane for shadow projection (point/spot)

    // Get bounding sphere for point light culling (world space)
    AABB getBoundingSphere(const glm::vec3& worldPosition) const {
        if (type == LightType::Point) {
            // Create AABB from sphere centered at worldPosition with radius
            glm::vec3 halfExtents(radius);
            return AABB(worldPosition - halfExtents, worldPosition + halfExtents);
        }
        // Directional lights affect everything, no culling
        return AABB(glm::vec3(-FLT_MAX), glm::vec3(FLT_MAX));
    }

    LightComponent() = default;

    // Helper constructors
    // Create directional light
    // @param dir Direction vector
    // @param col RGB color (0-1 linear)
    // @param illuminance Illuminance in lux (typical: 500-100000)
    static LightComponent createDirectionalLight(const glm::vec3& dir, const glm::vec3& col = glm::vec3(1.0f), float illuminance = 30000.0f) {
        LightComponent light;
        light.type = LightType::Directional;
        light.direction = glm::normalize(dir);
        light.color = col;
        light.intensity = illuminance;
        return light;
    }

    // Create point light
    // @param col RGB color (0-1 linear)
    // @param luminousPower Luminous power in lumens (typical: 100-5000)
    // @param rad Influence radius in world units
    static LightComponent createPointLight(const glm::vec3& col = glm::vec3(1.0f), float luminousPower = 800.0f, float rad = 100.0f) {
        LightComponent light;
        light.type = LightType::Point;
        light.color = col;
        light.intensity = luminousPower;
        light.radius = rad;
        return light;
    }
};

} // namespace violet
