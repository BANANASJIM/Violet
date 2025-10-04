#pragma once

#include <GLFW/glfw3.h>

#include <EASTL/functional.h>
#include <EASTL/string.h>

namespace violet {

class Window {
public:
    using ResizeCallback = eastl::function<void(int width, int height)>;

    Window(const Window&)            = default;
    Window(Window&&)                 = delete;
    Window& operator=(const Window&) = delete;
    Window& operator=(Window&&)      = delete;
    Window(int width, int height, const eastl::string& title);
    ~Window();

    void               setResizeCallback(const ResizeCallback& callback);
    void               pollEvents();
    [[nodiscard]] bool shouldClose() const;
    void               waitEvents();

    [[nodiscard]] GLFWwindow* getHandle() const { return window; }
    void                      getFramebufferSize(int* width, int* height) const;
    void                      getSize(int* width, int* height) const;

private:
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);

private:
    GLFWwindow*    window = nullptr;
    ResizeCallback resizeCallback;
};

} // namespace violet
