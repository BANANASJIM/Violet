#include "CameraController.hpp"

#include <GLFW/glfw3.h>

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include "core/Log.hpp"

namespace violet {

CameraController::CameraController(Camera* camera) : camera(camera) {
    if (camera) {
        position = camera->getPosition();
        updateCameraVectors();
    }

    keyPressedHandler = EventDispatcher::subscribe<KeyPressedEvent>([this](const KeyPressedEvent& event) {
        return onKeyPressed(event);
    });
    keyReleasedHandler = EventDispatcher::subscribe<KeyReleasedEvent>([this](const KeyReleasedEvent& event) {
        return onKeyReleased(event);
    });
    mousePressedHandler = EventDispatcher::subscribe<MousePressedEvent>([this](const MousePressedEvent& event) {
        return onMousePressed(event);
    });
    mouseReleasedHandler = EventDispatcher::subscribe<MouseReleasedEvent>([this](const MouseReleasedEvent& event) {
        return onMouseReleased(event);
    });
    mouseMovedHandler = EventDispatcher::subscribe<MouseMovedEvent>([this](const MouseMovedEvent& event) {
        return onMouseMoved(event);
    });
    scrollHandler = EventDispatcher::subscribe<ScrollEvent>([this](const ScrollEvent& event) {
        return onScroll(event);
    });
}

CameraController::~CameraController() {
    EventDispatcher::unsubscribe<KeyPressedEvent>(keyPressedHandler);
    EventDispatcher::unsubscribe<KeyReleasedEvent>(keyReleasedHandler);
    EventDispatcher::unsubscribe<MousePressedEvent>(mousePressedHandler);
    EventDispatcher::unsubscribe<MouseReleasedEvent>(mouseReleasedHandler);
    EventDispatcher::unsubscribe<MouseMovedEvent>(mouseMovedHandler);
    EventDispatcher::unsubscribe<ScrollEvent>(scrollHandler);
}

void CameraController::update(float deltaTime) {
    if (!camera)
        return;

    float velocity = movementSpeed * deltaTime;
    bool moved = false;

    if (heldKeys[GLFW_KEY_W]) {
        position += front * velocity;
        moved = true;
    }
    if (heldKeys[GLFW_KEY_S]) {
        position -= front * velocity;
        moved = true;
    }
    if (heldKeys[GLFW_KEY_A]) {
        position -= right * velocity;
        moved = true;
    }
    if (heldKeys[GLFW_KEY_D]) {
        position += right * velocity;
        moved = true;
    }
    if (heldKeys[GLFW_KEY_SPACE]) {
        position += worldUp * velocity;
        moved = true;
    }
    if (heldKeys[GLFW_KEY_LEFT_SHIFT]) {
        position -= worldUp * velocity;
        moved = true;
    }

    if (moved) {
        updateCameraVectors();
    }
}

void CameraController::setPosition(const glm::vec3& pos) {
    position = pos;
    updateCameraVectors();
}

bool CameraController::onKeyPressed(const KeyPressedEvent& event) {
    heldKeys[event.key] = true;
    return false;
}

bool CameraController::onKeyReleased(const KeyReleasedEvent& event) {
    heldKeys[event.key] = false;
    return false;
}

bool CameraController::onMousePressed(const MousePressedEvent& event) {
    if (ImGui::GetIO().WantCaptureMouse) {
        return false;
    }

    if (event.button == MouseButton::Right) {
        rightMouseHeld = true;
        firstUpdate = true;
    }
    return false;
}

bool CameraController::onMouseReleased(const MouseReleasedEvent& event) {
    if (event.button == MouseButton::Right) {
        rightMouseHeld = false;
        firstUpdate = true;
    }
    return false;
}

bool CameraController::onMouseMoved(const MouseMovedEvent& event) {
    if (ImGui::GetIO().WantCaptureMouse) {
        return false;
    }

    if (!rightMouseHeld) {
        return false;
    }

    if (firstUpdate) {
        firstUpdate = false;
        return false;
    }

    if (event.delta.x == 0.0f && event.delta.y == 0.0f) {
        return false;
    }

    float yawDelta = event.delta.x * sensitivity;
    float pitchDelta = event.delta.y * sensitivity;

    yaw += yawDelta;
    pitch -= pitchDelta;
    pitch = glm::clamp(pitch, -maxPitch, maxPitch);
    updateCameraVectors();

    return false;
}

bool CameraController::onScroll(const ScrollEvent& event) {
    if (ImGui::GetIO().WantCaptureMouse) {
        return false;
    }

    if (event.offset.y != 0.0f) {
        float speedMultiplier = 1.0f + (event.offset.y * 0.2f);
        movementSpeed *= speedMultiplier;
        movementSpeed = glm::clamp(movementSpeed, 1.0f, 1000.0f);
        VT_INFO("Movement speed adjusted to {:.1f}", movementSpeed);
    }

    return false;
}

void CameraController::updateCameraVectors() {
    // glTF Y-up coordinate system: +Y up, +X right, -Z forward
    glm::vec3 newFront;
    newFront.x = cos(yaw) * cos(pitch);
    newFront.y = sin(pitch);            // Y component for pitch (up/down)
    newFront.z = sin(yaw) * cos(pitch); // Z component for yaw (left/right)
    front      = glm::normalize(newFront);

    right = glm::normalize(glm::cross(front, worldUp));
    up    = glm::normalize(glm::cross(right, front));

    if (camera) {
        camera->setPosition(position);
        camera->setTarget(position + front);
        camera->setUp(up);

    }
}

} // namespace violet
