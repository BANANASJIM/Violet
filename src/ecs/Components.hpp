#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <EASTL/unique_ptr.h>
#include "renderer/Camera.hpp"
#include "renderer/CameraController.hpp"

namespace violet {

class VertexBuffer;
class Pipeline;
class DescriptorSet;
class Mesh;
class Material;

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

struct Renderable {
    Mesh* mesh = nullptr;
    Material* material = nullptr;

    // Legacy support - will be removed later
    VertexBuffer* vertexBuffer = nullptr;
    VertexBuffer* indexBuffer = nullptr;
    Pipeline* pipeline = nullptr;
    DescriptorSet* descriptorSet = nullptr;

    bool visible = true;
};

struct CameraComponent {
    eastl::unique_ptr<Camera> camera;
    bool isActive = false;

    CameraComponent() = default;
    CameraComponent(eastl::unique_ptr<Camera> cam) : camera(eastl::move(cam)) {}

    CameraComponent(CameraComponent&& other) noexcept
        : camera(eastl::move(other.camera)), isActive(other.isActive) {}

    CameraComponent& operator=(CameraComponent&& other) noexcept {
        if (this != &other) {
            camera = eastl::move(other.camera);
            isActive = other.isActive;
        }
        return *this;
    }

    CameraComponent(const CameraComponent&) = delete;
    CameraComponent& operator=(const CameraComponent&) = delete;
};

struct CameraControllerComponent {
    eastl::unique_ptr<CameraController> controller;

    CameraControllerComponent() = default;
    CameraControllerComponent(eastl::unique_ptr<CameraController> ctrl)
        : controller(eastl::move(ctrl)) {}

    CameraControllerComponent(CameraControllerComponent&& other) noexcept
        : controller(eastl::move(other.controller)) {}

    CameraControllerComponent& operator=(CameraControllerComponent&& other) noexcept {
        if (this != &other) {
            controller = eastl::move(other.controller);
        }
        return *this;
    }

    CameraControllerComponent(const CameraControllerComponent&) = delete;
    CameraControllerComponent& operator=(const CameraControllerComponent&) = delete;
};

}