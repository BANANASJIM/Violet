#include "CameraController.hpp"

#include <GLFW/glfw3.h>

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <imgui.h>

#include "input/Input.hpp"

using MouseButton = violet::MouseButton;

namespace violet {

CameraController::CameraController(Camera* camera) : camera(camera) {
    if (camera) {
        position = camera->getPosition();
        updateCameraVectors();
    }
}

void CameraController::update(float deltaTime) {
    if (!camera)
        return;

    processMouse();
    processKeyboard(deltaTime);
}

void CameraController::setPosition(const glm::vec3& pos) {
    position = pos;
    updateCameraVectors();
}

void CameraController::processKeyboard(float deltaTime) {
    float velocity = movementSpeed * deltaTime;
    bool moved = false;

    if (Input::isKeyHeld(GLFW_KEY_W)) {
        position += front * velocity;
        moved = true;
        spdlog::info("Camera moved forward");
    }
    if (Input::isKeyHeld(GLFW_KEY_S)) {
        position -= front * velocity;
        moved = true;
    }
    if (Input::isKeyHeld(GLFW_KEY_A)) {
        position -= right * velocity;
        moved = true;
    }
    if (Input::isKeyHeld(GLFW_KEY_D)) {
        position += right * velocity;
        moved = true;
    }
    if (Input::isKeyHeld(GLFW_KEY_SPACE)) {
        position += worldUp * velocity;
        moved = true;
    }
    if (Input::isKeyHeld(GLFW_KEY_LEFT_SHIFT)) {
        position -= worldUp * velocity;
        moved = true;
    }

    if (moved) {
        updateCameraVectors();
    }
}

void CameraController::processMouse() {
    // Don't process mouse input if ImGui wants it
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }

    // Only process mouse when right button is held
    if (!Input::isMouseButtonHeld(MouseButton::Right)) {
        firstUpdate = true; // Reset on right button release
        return;
    }

    glm::vec2 mouseDelta = Input::consumeMouseDelta();

    // Skip first frame after right button press to avoid jump
    if (firstUpdate) {
        firstUpdate = false;
        return;
    }

    if (mouseDelta.x == 0.0f && mouseDelta.y == 0.0f) {
        return;
    }

    float yawDelta = mouseDelta.x * sensitivity;
    float pitchDelta = mouseDelta.y * sensitivity;

    yaw += yawDelta;
    pitch -= pitchDelta;

    pitch = glm::clamp(pitch, -maxPitch, maxPitch);

    spdlog::debug("Camera: yaw:{:.1f}° pitch:{:.1f}°", glm::degrees(yaw), glm::degrees(pitch));

    updateCameraVectors();
}

void CameraController::updateCameraVectors() {
    // glTF Y-up coordinate system: +Y up, +X right, -Z forward
    glm::vec3 newFront;
    newFront.x = cos(yaw) * cos(pitch);
    newFront.y = sin(pitch);            // Y component for pitch (up/down)
    newFront.z = sin(yaw) * cos(pitch); // Z component for yaw (left/right)
    front = glm::normalize(newFront);

    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));

    if (camera) {
        camera->setPosition(position);
        camera->setTarget(position + front);
        camera->setUp(up);
    }
}

} // namespace violet
