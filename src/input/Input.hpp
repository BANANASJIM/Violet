#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <EASTL/unordered_map.h>

namespace violet {

enum class KeyState {
    Released,
    Pressed,
    Held
};

enum class MouseButton {
    Left = GLFW_MOUSE_BUTTON_LEFT,
    Right = GLFW_MOUSE_BUTTON_RIGHT,
    Middle = GLFW_MOUSE_BUTTON_MIDDLE
};

class Input {
public:
    static void initialize(GLFWwindow* window);
    static void update();
    static void shutdown();

    static bool isKeyPressed(int key);
    static bool isKeyHeld(int key);
    static bool isKeyReleased(int key);

    static bool isMouseButtonPressed(MouseButton button);
    static bool isMouseButtonHeld(MouseButton button);
    static bool isMouseButtonReleased(MouseButton button);

    static glm::vec2 getMousePosition();
    static glm::vec2 getMouseDelta();
    static glm::vec2 consumeMouseDelta();
    static void setMouseCursor(bool enabled);
    static bool isMouseCursorEnabled();

    static glm::vec2 getScrollDelta();
    static glm::vec2 consumeScrollDelta();

private:
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

private:
    static GLFWwindow* s_window;
    static eastl::unordered_map<int, KeyState> s_keyStates;
    static eastl::unordered_map<int, KeyState> s_mouseButtonStates;

    static glm::vec2 s_mousePosition;
    static glm::vec2 s_lastMousePosition;
    static glm::vec2 s_mouseDelta;
    static bool s_firstMouse;
    static bool s_cursorEnabled;

    static glm::vec2 s_scrollDelta;
};

}