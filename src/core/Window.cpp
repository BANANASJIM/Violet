#include "Window.hpp"
#include "input/InputManager.hpp"
#include <stdexcept>

namespace violet {

Window::Window(int width, int height, const std::string& title) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    if (!window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create window");
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    // Enable VSync for 60fps
    glfwSwapInterval(1);

    InputManager::initialize(window);
}

Window::~Window() {
    if (window) {
        InputManager::shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
    }
}

void Window::setResizeCallback(const ResizeCallback& callback) {
    resizeCallback = callback;
}

void Window::pollEvents() {
    glfwPollEvents();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window);
}

void Window::waitEvents() {
    glfwWaitEvents();
}

void Window::getFramebufferSize(int* width, int* height) const {
    glfwGetFramebufferSize(window, width, height);
}

void Window::getSize(int* width, int* height) const {
    glfwGetWindowSize(window, width, height);
}

void Window::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto windowObj = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (windowObj && windowObj->resizeCallback) {
        windowObj->resizeCallback(width, height);
    }
}

}