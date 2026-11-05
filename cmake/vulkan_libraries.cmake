# The libraries of the Vulkan renderer (src/render_vulkan; docs/design/vulkan.md "Libraries"), fetched at configure
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

# Vulkan Memory Allocator 3.2.1 (AMD, MIT): device memory for the renderer's buffers and images (header only; its
# implementation is compiled with the first code that allocates).
FetchContent_Declare(vulkan_memory_allocator
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.2.1.tar.gz
    URL_HASH SHA256=5e7749504cb802427ffb7bec38a0b6a15db46ae253f00560acb3e624d9fe695c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(vulkan_memory_allocator)
add_library(vulkan_memory_allocator INTERFACE)
target_include_directories(vulkan_memory_allocator INTERFACE "${vulkan_memory_allocator_SOURCE_DIR}/include")
target_link_libraries(vulkan_memory_allocator INTERFACE vulkan_headers)
