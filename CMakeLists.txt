cmake_minimum_required(VERSION 3.25)

# Set Clang as default compiler if not already specified
if(NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
    find_program(CLANG_C_COMPILER NAMES clang)
    if(CLANG_C_COMPILER)
        set(CMAKE_C_COMPILER ${CLANG_C_COMPILER})
    endif()
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
    find_program(CLANG_CXX_COMPILER NAMES clang++)
    if(CLANG_CXX_COMPILER)
        set(CMAKE_CXX_COMPILER ${CLANG_CXX_COMPILER})
    endif()
endif()

project(VioletEngine VERSION 0.1.0 LANGUAGES CXX)

# Detect vcpkg triplet for cross-platform support
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    if(UNIX AND NOT APPLE)
        set(VCPKG_TARGET_TRIPLET "x64-linux")
    elseif(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(VCPKG_TARGET_TRIPLET "arm64-osx")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-osx")
        endif()
    elseif(WIN32)
        set(VCPKG_TARGET_TRIPLET "x64-windows")
    endif()
    message(STATUS "Auto-detected vcpkg triplet: ${VCPKG_TARGET_TRIPLET}")
endif()

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++20 with all features
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find packages
# Force use system Vulkan to avoid glfw compatibility issues
if(UNIX AND NOT APPLE)
    set(Vulkan_LIBRARY "/usr/lib/libvulkan.so")
    set(Vulkan_INCLUDE_DIR "/usr/include")
endif()
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(EASTL REQUIRED)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
find_package(Ktx CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)

# Enable ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()


# ============================================
# Resolve vcpkg Shader Slang vs system S-Lang conflict
# ============================================
# Must be defined BEFORE add_executable to create imported target
# Explicitly find Shader Slang library in vcpkg directory to avoid
# linking against system S-Lang library (/usr/lib/libslang.so)

# Determine vcpkg lib directory based on build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(VCPKG_LIB_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/lib")
else()
    set(VCPKG_LIB_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")
endif()

# Find Shader Slang with full path (cross-platform)
find_library(SLANG_LIBRARY_FULL_PATH
    NAMES slang
    PATHS ${VCPKG_LIB_DIR}
    NO_DEFAULT_PATH
    REQUIRED
)

if(SLANG_LIBRARY_FULL_PATH)
    message(STATUS "Found Shader Slang: ${SLANG_LIBRARY_FULL_PATH}")

    # Create imported library target to force CMake to use absolute path
    add_library(ShaderSlang SHARED IMPORTED)
    set_target_properties(ShaderSlang PROPERTIES
        IMPORTED_LOCATION "${SLANG_LIBRARY_FULL_PATH}"
    )
else()
    message(FATAL_ERROR "vcpkg Shader Slang library not found in ${VCPKG_LIB_DIR}")
endif()

# Collect source files
file(GLOB_RECURSE VIOLET_SOURCES
    "src/*.cpp"
    "src/*.hpp"
    "src/*.h"
)

# Create executable
add_executable(${PROJECT_NAME} ${VIOLET_SOURCES})

# Set RPATH - system first for Vulkan compatibility
if(UNIX AND NOT APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        CMAKE_SKIP_BUILD_RPATH FALSE
        CMAKE_BUILD_WITH_INSTALL_RPATH FALSE
        # vcpkg path first to prioritize Shader Slang over system S-Lang
        # System Vulkan used via vcpkg overlay (empty port prevents vcpkg Vulkan installation)
        BUILD_RPATH "${VCPKG_LIB_DIR}:/usr/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
    # Force use of RPATH instead of RUNPATH
    target_link_options(${PROJECT_NAME} PRIVATE
        -Wl,--disable-new-dtags
    )

    # Explicitly add Slang library directory with absolute path to DT_NEEDED
    # This ensures runtime linker finds Slang using full path, not RPATH
    target_link_options(${PROJECT_NAME} PRIVATE
        "LINKER:-rpath-link,${VCPKG_LIB_DIR}"
    )
elseif(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        CMAKE_SKIP_BUILD_RPATH FALSE
        CMAKE_BUILD_WITH_INSTALL_RPATH FALSE
        CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE
        BUILD_RPATH "@loader_path/../lib:${VCPKG_LIB_DIR}"
    )
endif()

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${TINYGLTF_INCLUDE_DIRS}
    ${Stb_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    fmt::fmt
    GPUOpen::VulkanMemoryAllocator
    EASTL
    KTX::ktx
    EnTT::EnTT
    imgui::imgui
    imguizmo::imguizmo
)

# Link Slang with absolute path to avoid name conflict with system S-Lang
# Must be after other libraries to ensure proper link order
if(UNIX AND NOT APPLE)
    # Use absolute path directly to force DT_NEEDED entry with path
    target_link_options(${PROJECT_NAME} PRIVATE
        "${SLANG_LIBRARY_FULL_PATH}"
    )
elseif(APPLE OR WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ShaderSlang)
endif()

# Explicitly link C++ standard library on Linux when using Clang
if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_libraries(${PROJECT_NAME} PRIVATE stdc++)
endif()

# Precompiled headers for faster compilation
target_precompile_headers(${PROJECT_NAME} PRIVATE
    <vulkan/vulkan.hpp>
    <vulkan/vulkan_raii.hpp>
    <vk_mem_alloc.h>
    <spdlog/spdlog.h>
    <glm/glm.hpp>
    <algorithm>
    <array>
    <chrono>
    <concepts>
    <coroutine>
    <expected>
    <format>
    <functional>
    <memory>
    <optional>
    <ranges>
    <span>
    <string_view>
    <vector>
)

# Platform specific settings
if(APPLE)
    # MoltenVK configuration
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VK_ENABLE_BETA_EXTENSIONS
        MVK_MACOS=1
    )
endif()
# Note: GLFW automatically handles platform surface extensions (Wayland/XCB/Win32/etc)

# Debug/Release configurations
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:VIOLET_DEBUG>
    $<$<CONFIG:Release>:VIOLET_RELEASE>
    $<$<CONFIG:Debug>:VIOLET_ENABLE_VALIDATION>
)

# Shader compilation
add_custom_target(shaders)
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
)

foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${CMAKE_CURRENT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/"
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${GLSL} -o ${SPIRV}
        DEPENDS ${GLSL}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()

add_custom_target(compile_shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(${PROJECT_NAME} compile_shaders)

# Copy compile_commands.json to project root for clangd
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
)