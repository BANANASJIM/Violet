#include "examples/TestApp.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>

void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line) {
    return malloc(size);
}

void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line) {
    return aligned_alloc(alignment, size);
}

int main() {
    spdlog::set_level(spdlog::level::debug);

    violet::TestApp app;
    try {
        app.run();
    } catch (const std::exception& e) {
        spdlog::critical("Exception: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}