#pragma once

#include <glm/glm.hpp>

#include "Camera.hpp"

namespace violet {

class CameraController {
public:
    CameraController(Camera* camera);

    void update(float deltaTime);

    void setMovementSpeed(float speed) { movementSpeed = speed; }
    void setSensitivity(float sens) { sensitivity = sens; }

    float getMovementSpeed() const { return movementSpeed; }
    float getSensitivity() const { return sensitivity; }

    void setPosition(const glm::vec3& pos);
    void setYaw(float yawDegrees) {
        yaw = glm::radians(yawDegrees);
        updateCameraVectors();
    }
    void setPitch(float pitchDegrees) {
        pitch = glm::clamp(glm::radians(pitchDegrees), -maxPitch, maxPitch);
        updateCameraVectors();
    }

    float getYaw() const { return glm::degrees(yaw); }
    float getPitch() const { return glm::degrees(pitch); }

private:
    void processKeyboard(float deltaTime);
    void processMouse();
    void processScroll();
    void updateCameraVectors();

private:
    Camera* camera;

    glm::vec3 position{0.0f, 0.0f, 3.0f};
    glm::vec3 front{0.0f, 0.0f, -1.0f};  // glTF: -Z is forward into screen
    glm::vec3 up{0.0f, 1.0f, 0.0f};      // glTF: +Y is up
    glm::vec3 right{1.0f, 0.0f, 0.0f};   // glTF: +X is right
    glm::vec3 worldUp{0.0f, 1.0f, 0.0f}; // glTF: Y-up coordinate system

    float yaw = -glm::pi<float>() / 2.0f;
    float pitch = 0.0f;

    float movementSpeed = 5.0f;
    float sensitivity = 0.0001f;

    static constexpr float maxPitch = glm::pi<float>() / 2.0f - 0.01f;

    bool firstUpdate = true;
};

} // namespace violet
