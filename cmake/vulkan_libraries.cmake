# The libraries of the Vulkan renderer (src/gpu_vulkan; docs/design/vulkan.md "Libraries"), fetched at configure
# time and checked against their SHA-256, as the other fetched code is (THIRD_PARTY_NOTICES.md). Only the standard
# Vulkan API and portable C / C++: nothing here needs MSVC.
include(FetchContent)

# Vulkan-Headers 1.4.304 (Khronos, Apache-2.0; some files also MIT): vulkan_core.h and the registry's headers. Only
# the include folder is used (SOURCE_SUBDIR: the repository's own CMake files install and test the registry).
FetchContent_Declare(vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.304.tar.gz
    URL_HASH SHA256=5e1a06b3f27e7581b55d1dea176fd97ee0a2f299406db2f437c1d2f297ceba5b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(vulkan_headers)
add_library(vulkan_headers INTERFACE)
target_include_directories(vulkan_headers INTERFACE "${vulkan_headers_SOURCE_DIR}/include")
# No prototypes: every Vulkan function is a pointer volk loads (from vulkan-1.dll, then per instance and device).
target_compile_definitions(vulkan_headers INTERFACE VK_NO_PROTOTYPES)

# volk 1.4.304 (Arseny Kapoulkine, MIT): the Vulkan meta-loader - vulkan-1.dll opened at run time, the function
# pointers loaded per instance and per device. Its one source compiled here (its CMake files look for an installed SDK).
FetchContent_Declare(volk
    URL https://github.com/zeux/volk/archive/refs/tags/1.4.304.tar.gz
    URL_HASH SHA256=ab3d4a8ccaeb32652259cdd008399504a41792675b0421d90b67729ee274746f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(volk)
add_library(volk STATIC "${volk_SOURCE_DIR}/volk.c")
target_include_directories(volk PUBLIC "${volk_SOURCE_DIR}")
target_link_libraries(volk PUBLIC vulkan_headers)
target_compile_options(volk PRIVATE /W0)

# Vulkan Memory Allocator 3.2.1 (AMD, MIT): device memory for the renderer's buffers and images. Its implementation is
# compiled here, once (a source made in the build folder, as stb_image_write's): no Vulkan function is linked
# (VMA_STATIC_VULKAN_FUNCTIONS 0 - there is no import library), the allocator finds them through the
# vkGetInstanceProcAddr / vkGetDeviceProcAddr that volk loaded, given at its creation (VMA_DYNAMIC_VULKAN_FUNCTIONS 1).
FetchContent_Declare(vulkan_memory_allocator
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.2.1.tar.gz
    URL_HASH SHA256=5e7749504cb802427ffb7bec38a0b6a15db46ae253f00560acb3e624d9fe695c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(vulkan_memory_allocator)
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/vk_mem_alloc_impl.cpp
    CONTENT "#define VMA_IMPLEMENTATION\n#include \"vk_mem_alloc.h\"\n")
add_library(vulkan_memory_allocator STATIC ${CMAKE_BINARY_DIR}/vk_mem_alloc_impl.cpp)
target_include_directories(vulkan_memory_allocator PUBLIC "${vulkan_memory_allocator_SOURCE_DIR}/include")
target_compile_definitions(vulkan_memory_allocator PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
target_link_libraries(vulkan_memory_allocator PUBLIC vulkan_headers)
if(MSVC)
    target_compile_options(vulkan_memory_allocator PRIVATE /W0)
else()
    target_compile_options(vulkan_memory_allocator PRIVATE -w)
endif()

# glslang (The Khronos Group and contributors; BSD-3-Clause, BSD-2-Clause, MIT, Apache-2.0 - THIRD_PARTY_NOTICES.md),
# the Vulkan SDK 1.4.304 release: the GLSL -> SPIR-V compiler the fixed-function emulation's shaders are compiled with
# at run time (src/gpu_vulkan/glsl_compiler.h). Only its library: no HLSL front end, no optimiser (SPIRV-Tools), no
# programs, no install, no tests. Its build_info.h is made by CMake's configure_file (no Python needed).
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
# (built with exceptions and RTTI like the code that links it: the C++ library's inline code stays one definition)
set(ENABLE_EXCEPTIONS ON CACHE BOOL "" FORCE)
set(ENABLE_RTTI ON CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glslang
    URL https://github.com/KhronosGroup/glslang/archive/refs/tags/vulkan-sdk-1.4.304.0.tar.gz
    URL_HASH SHA256=30f6557836308d9c8e8ea7e8905d2d430bd60752506b01ed9be6c4e82fe732ac
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(glslang)
foreach(glslang_target IN ITEMS glslang glslang-default-resource-limits)
    if(MSVC)
        target_compile_options(${glslang_target} PRIVATE /W0)
    else()
        target_compile_options(${glslang_target} PRIVATE -w)
    endif()
endforeach()

# SPIRV-Tools and SPIRV-Headers (The Khronos Group; Apache-2.0 - THIRD_PARTY_NOTICES.md), the Vulkan SDK 1.4.304
# release: only the SPIR-V validator (spirv-val's library), linked by the fixed-function shader check alone
# (src/tests/ff_shader_check.cpp) - nothing in the game uses it. Its build makes its grammar tables with Python 3:
# without Python, or with THUG2_SPIRV_VALIDATOR off, the check runs without validating.
option(THUG2_SPIRV_VALIDATOR "Build SPIRV-Tools' validator for the fixed-function shader check (needs Python 3)" ON)
if(THUG2_SPIRV_VALIDATOR)
    find_package(Python3 COMPONENTS Interpreter)
    if(NOT Python3_FOUND)
        message(STATUS "Python 3 not found: the fixed-function shader check runs without the SPIR-V validator")
        set(THUG2_SPIRV_VALIDATOR OFF)
    endif()
endif()
if(THUG2_SPIRV_VALIDATOR)
    FetchContent_Declare(spirv_headers
        URL https://github.com/KhronosGroup/SPIRV-Headers/archive/refs/tags/vulkan-sdk-1.4.304.0.tar.gz
        URL_HASH SHA256=162b864ebaf339d66953fc2c4ad974bc4f453e0f04155cd3755a85e33f408eee
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR no-cmake)
    FetchContent_MakeAvailable(spirv_headers)
    set(SPIRV-Headers_SOURCE_DIR "${spirv_headers_SOURCE_DIR}")
    set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
    set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
    set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(spirv_tools
        URL https://github.com/KhronosGroup/SPIRV-Tools/archive/refs/tags/vulkan-sdk-1.4.304.0.tar.gz
        URL_HASH SHA256=ad6e8922538c498e7131bcd82a8d6d9f9863b8d7431c5bfa27dd98e26435be07
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(spirv_tools)
    if(MSVC)
        target_compile_options(SPIRV-Tools-static PRIVATE /W0)
    else()
        target_compile_options(SPIRV-Tools-static PRIVATE -w)
    endif()
endif()
