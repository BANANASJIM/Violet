#pragma once

#include <GLFW/glfw3.h>
#include <EASTL/string.h>
#include <EASTL/functional.h>

namespace violet {

class Window {
public:
    using ResizeCallback = eastl::function<void(int width, int height)>;

    Window(int width, int height, const eastl::string& title);
    ~Window();

    void setResizeCallback(const ResizeCallback& callback);
    void pollEvents();
    bool shouldClose() const;
    void waitEvents();

    GLFWwindow* getHandle() const { return window; }
    void getFramebufferSize(int* width, int* height) const;
    void getSize(int* width, int* height) const;

private:
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);

private:
    GLFWwindow* window = nullptr;
    ResizeCallback resizeCallback;
};

}