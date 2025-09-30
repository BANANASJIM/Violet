#pragma once

#include "InputEvents.hpp"
#include "core/events/EventDispatcher.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace violet {

class InputManager {
public:
    static void initialize(GLFWwindow* window);
    static void processEvents();
    static void shutdown();

    // Current mouse position for convenience
    static glm::vec2 getMousePosition() { return s_mousePosition; }

private:
    // GLFW callbacks - only collect events and convert to our event system
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double x, double y);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    // Utility functions
    static uint32_t getCurrentTimestamp();

    // State
    static GLFWwindow* s_window;
    static glm::vec2 s_mousePosition;
    static glm::vec2 s_lastMousePosition;
    static bool s_firstMouse;
};

}