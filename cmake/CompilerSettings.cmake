# Modern C++20 compiler settings

# This function will be called after target creation
function(apply_compiler_settings target_name)

# Compiler detection  
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang/AppleClang settings
    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-missing-field-initializers
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-private-field
        
        # C++20 features
        -std=c++20
        -stdlib=libc++
        -fcoroutines
        
        # Optimizations
        $<$<CONFIG:Debug>:-O0 -g3 -fno-omit-frame-pointer>
        $<$<CONFIG:Release>:-O3 -march=native -flto>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g>
        
        # Sanitizers in debug
        $<$<CONFIG:Debug>:-fsanitize=address>
        $<$<CONFIG:Debug>:-fsanitize=undefined>
    )
    
    target_link_options(${target_name} PRIVATE
        -stdlib=libc++
        $<$<CONFIG:Debug>:-fsanitize=address>
        $<$<CONFIG:Debug>:-fsanitize=undefined>
        $<$<CONFIG:Release>:-flto>
    )
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC settings
    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-missing-field-initializers
        -Wno-unused-parameter
        
        # C++20 features
        -std=c++20
        -fcoroutines
        
        # Optimizations
        $<$<CONFIG:Debug>:-O0 -g3>
        $<$<CONFIG:Release>:-O3 -march=native -flto>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    )
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # MSVC settings
    target_compile_options(${target_name} PRIVATE
        /W4
        /std:c++20
        /permissive-
        /Zc:__cplusplus
        
        # Coroutines
        /await:strict
        
        # Optimizations
        $<$<CONFIG:Debug>:/Od /MDd /Zi /RTC1>
        $<$<CONFIG:Release>:/O2 /MD /GL>
    )
    
    target_link_options(${target_name} PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
endif()

# Enable color diagnostics
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fcolor-diagnostics)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fdiagnostics-color=always)
endif()

endfunction()