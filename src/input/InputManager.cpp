#include "InputManager.hpp"
#include "core/Log.hpp"

namespace violet {

GLFWwindow* InputManager::s_window = nullptr;
glm::vec2 InputManager::s_mousePosition{0.0f};
glm::vec2 InputManager::s_lastMousePosition{0.0f};
bool InputManager::s_firstMouse = true;

void InputManager::initialize(GLFWwindow* window) {
    s_window = window;

    // Register GLFW callbacks
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);


    // Initialize mouse position
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    s_mousePosition = glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
    s_lastMousePosition = s_mousePosition;

}

void InputManager::processEvents() {
    // This is called after glfwPollEvents(), no additional work needed
    // Events are already dispatched in callbacks
}

void InputManager::shutdown() {
    s_window = nullptr;
    s_firstMouse = true;
    EventDispatcher::clear();
}

void InputManager::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    uint32_t timestamp = getCurrentTimestamp();

    if (action == GLFW_PRESS) {
        KeyPressedEvent event(key, mods);
        event.timestamp = timestamp;
        EventDispatcher::publish(event);
    } else if (action == GLFW_RELEASE) {
        KeyReleasedEvent event(key, mods);
        event.timestamp = timestamp;
        EventDispatcher::publish(event);
    }
}

void InputManager::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    uint32_t timestamp = getCurrentTimestamp();

    if (action == GLFW_PRESS) {
        MousePressedEvent event(static_cast<MouseButton>(button), s_mousePosition, mods);
        event.timestamp = timestamp;
        EventDispatcher::publish(event);
    } else if (action == GLFW_RELEASE) {
        MouseReleasedEvent event(static_cast<MouseButton>(button), s_mousePosition, mods);
        event.timestamp = timestamp;
        EventDispatcher::publish(event);
    }
}

void InputManager::cursorPosCallback(GLFWwindow* window, double x, double y) {
    glm::vec2 currentPos(static_cast<float>(x), static_cast<float>(y));

    if (s_firstMouse) {
        s_lastMousePosition = currentPos;
        s_firstMouse = false;
    }

    glm::vec2 delta = currentPos - s_lastMousePosition;
    s_lastMousePosition = s_mousePosition;
    s_mousePosition = currentPos;

    // Only publish mouse move events if there's actual movement
    if (glm::length(delta) > 0.001f) {
        MouseMovedEvent event(currentPos, delta);
        event.timestamp = getCurrentTimestamp();
        EventDispatcher::publish(event);
    }
}

void InputManager::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    ScrollEvent event(glm::vec2(static_cast<float>(xoffset), static_cast<float>(yoffset)));
    event.timestamp = getCurrentTimestamp();
    EventDispatcher::publish(event);
    VT_DEBUG("Scroll event: ({}, {})", xoffset, yoffset);
}

uint32_t InputManager::getCurrentTimestamp() {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

}