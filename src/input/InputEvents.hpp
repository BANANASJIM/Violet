#pragma once

#include "core/events/Event.hpp"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

namespace violet {

enum class MouseButton : int {
    Left = GLFW_MOUSE_BUTTON_LEFT,
    Right = GLFW_MOUSE_BUTTON_RIGHT,
    Middle = GLFW_MOUSE_BUTTON_MIDDLE
};

// Mouse Events
struct MousePressedEvent : public Event {
    MouseButton button;
    glm::vec2 position;
    int mods;

    MousePressedEvent(MouseButton btn, glm::vec2 pos, int modifiers)
        : button(btn), position(pos), mods(modifiers) {}
};

struct MouseReleasedEvent : public Event {
    MouseButton button;
    glm::vec2 position;
    int mods;

    MouseReleasedEvent(MouseButton btn, glm::vec2 pos, int modifiers)
        : button(btn), position(pos), mods(modifiers) {}
};

struct MouseMovedEvent : public Event {
    glm::vec2 position;
    glm::vec2 delta;

    MouseMovedEvent(glm::vec2 pos, glm::vec2 d)
        : position(pos), delta(d) {}
};

struct ScrollEvent : public Event {
    glm::vec2 offset;

    ScrollEvent(glm::vec2 off)
        : offset(off) {}
};

// Keyboard Events
struct KeyPressedEvent : public Event {
    int key;
    int mods;

    KeyPressedEvent(int k, int modifiers)
        : key(k), mods(modifiers) {}
};

struct KeyReleasedEvent : public Event {
    int key;
    int mods;

    KeyReleasedEvent(int k, int modifiers)
        : key(k), mods(modifiers) {}
};

}