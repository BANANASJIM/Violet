#pragma once

#include <GLFW/glfw3.h>

namespace violet {

class Timer {
public:
    Timer() : lastTime(glfwGetTime()) {}

    float tick() {
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;
        return deltaTime;
    }

    void reset() {
        lastTime = glfwGetTime();
    }

    double getTime() const {
        return glfwGetTime();
    }

private:
    double lastTime;
};

} // namespace violet