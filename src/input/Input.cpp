#include "Input.hpp"
#include <spdlog/spdlog.h>

namespace violet {

GLFWwindow* Input::s_window = nullptr;
eastl::unordered_map<int, KeyState> Input::s_keyStates;
eastl::unordered_map<int, KeyState> Input::s_mouseButtonStates;

glm::vec2 Input::s_mousePosition{0.0f};
glm::vec2 Input::s_lastMousePosition{0.0f};
glm::vec2 Input::s_mouseDelta{0.0f};
bool Input::s_firstMouse = true;
bool Input::s_cursorEnabled = true;

void Input::initialize(GLFWwindow* window) {
    s_window = window;

    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);

    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    s_mousePosition = glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
    s_lastMousePosition = s_mousePosition;
}

void Input::update() {
    // Use iterator-safe removal for key states
    for (auto it = s_keyStates.begin(); it != s_keyStates.end();) {
        if (it->second == KeyState::Pressed) {
            it->second = KeyState::Held;
            ++it;
        } else if (it->second == KeyState::Released) {
            it = s_keyStates.erase(it);
        } else {
            ++it;
        }
    }

    // Use iterator-safe removal for mouse button states
    for (auto it = s_mouseButtonStates.begin(); it != s_mouseButtonStates.end();) {
        if (it->second == KeyState::Pressed) {
            it->second = KeyState::Held;
            ++it;
        } else if (it->second == KeyState::Released) {
            it = s_mouseButtonStates.erase(it);
        } else {
            ++it;
        }
    }

}

void Input::shutdown() {
    s_keyStates.clear();
    s_mouseButtonStates.clear();
    s_window = nullptr;
}

bool Input::isKeyPressed(int key) {
    auto it = s_keyStates.find(key);
    return it != s_keyStates.end() && it->second == KeyState::Pressed;
}

bool Input::isKeyHeld(int key) {
    auto it = s_keyStates.find(key);
    return it != s_keyStates.end() && (it->second == KeyState::Pressed || it->second == KeyState::Held);
}

bool Input::isKeyReleased(int key) {
    auto it = s_keyStates.find(key);
    return it != s_keyStates.end() && it->second == KeyState::Released;
}

bool Input::isMouseButtonPressed(MouseButton button) {
    auto it = s_mouseButtonStates.find(static_cast<int>(button));
    return it != s_mouseButtonStates.end() && it->second == KeyState::Pressed;
}

bool Input::isMouseButtonHeld(MouseButton button) {
    auto it = s_mouseButtonStates.find(static_cast<int>(button));
    return it != s_mouseButtonStates.end() && (it->second == KeyState::Pressed || it->second == KeyState::Held);
}

bool Input::isMouseButtonReleased(MouseButton button) {
    auto it = s_mouseButtonStates.find(static_cast<int>(button));
    return it != s_mouseButtonStates.end() && it->second == KeyState::Released;
}

glm::vec2 Input::getMousePosition() {
    return s_mousePosition;
}

glm::vec2 Input::getMouseDelta() {
    return s_mouseDelta;
}

glm::vec2 Input::consumeMouseDelta() {
    glm::vec2 delta = s_mouseDelta;
    s_mouseDelta = glm::vec2(0.0f);
    return delta;
}

void Input::setMouseCursor(bool enabled) {
    if (s_cursorEnabled != enabled) {
        s_cursorEnabled = enabled;
        if (s_window) {
            glfwSetInputMode(s_window, GLFW_CURSOR, enabled ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (!enabled) {
                s_firstMouse = true;
            }
        }
    }
}

bool Input::isMouseCursorEnabled() {
    return s_cursorEnabled;
}

void Input::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        s_keyStates[key] = KeyState::Pressed;
    } else if (action == GLFW_RELEASE) {
        s_keyStates[key] = KeyState::Released;
    }
}

void Input::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (action == GLFW_PRESS) {
        s_mouseButtonStates[button] = KeyState::Pressed;
    } else if (action == GLFW_RELEASE) {
        s_mouseButtonStates[button] = KeyState::Released;
    }
}

void Input::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    glm::vec2 currentPos(static_cast<float>(xpos), static_cast<float>(ypos));

    if (s_firstMouse) {
        s_lastMousePosition = currentPos;
        s_firstMouse = false;
    }

    s_mouseDelta = currentPos - s_lastMousePosition;
    s_lastMousePosition = currentPos;
    s_mousePosition = currentPos;
}

}