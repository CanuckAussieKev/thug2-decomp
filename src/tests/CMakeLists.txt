# The checks (src/tests): offline programs that compare the project's own formats, parsing, formatting and maths
# with references. Each links only the libraries it checks.
# NOT ORIGINAL - thug2_format_check.exe: offline checks of the file, save and network format readers / writers
# (src/tests/format_check.cpp; give it save files or folders: `thug2_format_check <game>/Save`).
add_executable(thug2_format_check
    format_check.cpp
    format_check_net.cpp
    format_check_files.cpp)
target_link_libraries(thug2_format_check PRIVATE thug2_wire)

# NOT ORIGINAL - thug2_maths_check.exe: the plain C++ that replaced the x87 inline assembly compared with that
# assembly over many inputs, and maths::matrix_multiply with D3DX's matrix product (src/tests/maths_check.cpp; the
# reference half needs MSVC for 32-bit x86).
add_executable(thug2_maths_check maths_check.cpp maths_check_d3dx.cpp)
# (d3dx9: D3DX9's own maths, the reference for maths::matrix_multiply and base/maths/view_transforms.h; its DLL next to
# the check)
target_link_libraries(thug2_maths_check PRIVATE thug2_base d3dx9)
add_custom_command(TARGET thug2_maths_check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_D3DX9_NATIVE}/release/bin/x86/D3DX9_43.dll" "$<TARGET_FILE_DIR:thug2_maths_check>/d3dx9_43.dll")

# NOT ORIGINAL - thug2_parse_check.exe: the number parsing that replaced atoi / atol / atof (base/core/
# parse_number.h) compared with the C runtime's over many texts (src/tests/parse_check.cpp).
add_executable(thug2_parse_check parse_check.cpp)
target_link_libraries(thug2_parse_check PRIVATE thug2_formats)

# NOT ORIGINAL - thug2_printf_check.exe: the printf-style formatting without C varargs (base/core/format.h) compared
# with the C runtime's snprintf over every conversion, flag, width, precision and edge value (src/tests/printf_check.cpp).
add_executable(thug2_printf_check printf_check.cpp)
target_link_libraries(thug2_printf_check PRIVATE thug2_formats)

# NOT ORIGINAL - thug2_vulkan_context_check.exe: the Vulkan backend's context tried on this machine - an SDL window with
# a Vulkan surface, the instance (validation when installed), the device, the swapchain, frames cleared and presented
# across a resize, everything released - and what it found printed: the devices, their queue families and the depth
# and texture formats the renderer needs (src/tests/vulkan_context_check.cpp). Needs no Vulkan SDK, only a driver.
if(THUG2_VULKAN)
    add_executable(thug2_vulkan_context_check vulkan_context_check.cpp)
    target_link_libraries(thug2_vulkan_context_check PRIVATE thug2_render_vulkan)
    # (its own main(): SDL's main wrapper isn't used)
    target_compile_definitions(thug2_vulkan_context_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_ff_shader_check.exe: the Vulkan backend's fixed-function emulation checked - every shader key the
# Direct3D 9 backend's draws can make (a model of src/render_pc's draws), its GLSL generated and compiled, the SPIR-V
# validated by SPIRV-Tools (when built: THUG2_SPIRV_VALIDATOR, cmake/vulkan_libraries.cmake) and handed to the Vulkan
# driver as modules and pipelines (src/tests/ff_shader_check.cpp).
if(THUG2_VULKAN)
    add_executable(thug2_ff_shader_check ff_shader_check.cpp ff_shader_check_model.cpp ff_shader_check_gpu.cpp)
    target_link_libraries(thug2_ff_shader_check PRIVATE thug2_render_vulkan sdl2)
    target_compile_definitions(thug2_ff_shader_check PRIVATE SDL_MAIN_HANDLED)
    if(THUG2_SPIRV_VALIDATOR)
        target_link_libraries(thug2_ff_shader_check PRIVATE SPIRV-Tools-static)
        target_compile_definitions(thug2_ff_shader_check PRIVATE THUG2_SPIRV_VALIDATOR)
    endif()
endif()
