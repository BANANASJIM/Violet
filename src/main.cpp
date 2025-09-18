#include "examples/VioletApp.hpp"
#include "core/Log.hpp"
#include <cstdlib>

void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line) {
    return malloc(size);
}

void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line) {
    return aligned_alloc(alignment, size);
}

int main() {
    violet::Log::init();

    violet::VioletApp app;
    try {
        app.run();
    } catch (const std::exception& e) {
        VT_CRITICAL("Exception: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}