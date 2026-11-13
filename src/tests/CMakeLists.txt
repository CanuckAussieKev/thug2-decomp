# The checks (src/tests): offline programs that compare the project's own formats, parsing, formatting and maths
# with references. Each links only the libraries it checks, and none ever waits on an error dialog (no_error_dialogs.h).
# Each is built when the libraries it links are (on x64 only the x64-ready ones: THUG2_X64_READY in the root).
# NOT ORIGINAL - thug2_format_check.exe: offline checks of the file, save and network format readers / writers
# (src/tests/format_check.cpp; give it save files or folders: `thug2_format_check <game>/Save`).
if(TARGET thug2_wire)
    add_executable(thug2_format_check
        format_check.cpp
        format_check_net.cpp
        format_check_files.cpp
        format_check_demo.cpp)
    target_link_libraries(thug2_format_check PRIVATE thug2_wire)
endif()

# NOT ORIGINAL - thug2_video_check.exe: a video the game wrote (a demo saved as a video, -encode-video) read back with
# the game's own FFmpeg DLLs - codec, size, frame rate, frame count - and frames saved as PNG (src/tests/video_check.cpp).
if(TARGET thug2_platform)
    add_executable(thug2_video_check video_check.cpp "${PROJECT_SOURCE_DIR}/src/media_ffmpeg/ffmpeg_api.cpp")
    target_link_libraries(thug2_video_check PRIVATE thug2_platform ffmpeg_headers)
    if(THUG2_FFMPEG_DLLS)
        add_custom_command(TARGET thug2_video_check POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THUG2_FFMPEG_DLLS} "$<TARGET_FILE_DIR:thug2_video_check>")
    endif()
endif()

# NOT ORIGINAL - thug2_maths_check.exe: the plain C++ that replaced the x87 inline assembly compared with that
# assembly over many inputs, and maths::matrix_multiply with D3DX's matrix product (src/tests/maths_check.cpp; the
# reference half needs MSVC for 32-bit x86); with --write-corpus / --compare-corpus the corpus of the 32-bit build's
# results that the 64-bit build compares with (maths_corpus.h).
if(TARGET thug2_base)
    add_executable(thug2_maths_check maths_check.cpp maths_check_d3dx.cpp maths_corpus.cpp maths_corpus_cases.cpp)
    # (d3dx9: D3DX9's own maths, the reference for maths::matrix_multiply and base/maths/view_transforms.h; its DLL
    # next to the check)
    target_link_libraries(thug2_maths_check PRIVATE thug2_base d3dx9)
    add_custom_command(TARGET thug2_maths_check POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_D3DX9_NATIVE}/release/bin/${THUG2_ARCH}/D3DX9_43.dll" "$<TARGET_FILE_DIR:thug2_maths_check>/d3dx9_43.dll")
endif()

# NOT ORIGINAL - thug2_parse_check.exe: the number parsing that replaced atoi / atol / atof (base/core/
# parse_number.h) compared with the C runtime's over many texts (src/tests/parse_check.cpp).
if(TARGET thug2_base)
    add_executable(thug2_parse_check parse_check.cpp)
    target_link_libraries(thug2_parse_check PRIVATE thug2_base)
endif()

# NOT ORIGINAL - thug2_printf_check.exe: the printf-style formatting without C varargs (base/core/format.h) compared
# with the C runtime's snprintf over every conversion, flag, width, precision and edge value (src/tests/printf_check.cpp).
if(TARGET thug2_base)
    add_executable(thug2_printf_check printf_check.cpp)
    target_link_libraries(thug2_printf_check PRIVATE thug2_base)
endif()

# NOT ORIGINAL - thug2_rt_scene_check.exe: ray tracing's pure helpers (engine/gfx/rt_checks.h: a Direct3D matrix as an
# instance's rows, strips as lists, the contract check's geometry and flag tests; the baked sun's fit, engine/gfx/rt_sun)
# checked (src/tests/rt_scene_check.cpp).
if(TARGET thug2_base)
    add_executable(thug2_rt_scene_check rt_scene_check.cpp "${PROJECT_SOURCE_DIR}/src/engine/gfx/rt_sun.cpp")
    target_link_libraries(thug2_rt_scene_check PRIVATE thug2_base)
endif()

# NOT ORIGINAL - thug2_crash_check.exe: the game's crash report (src/platform/crash.h) caused on purpose, a few calls
# deep, into thug2-decomp.log next to the check (src/tests/crash_check.cpp; `thug2_crash_check [kind]`).
if(TARGET thug2_platform)
    add_executable(thug2_crash_check crash_check.cpp)
    target_link_libraries(thug2_crash_check PRIVATE thug2_platform)
endif()

# NOT ORIGINAL - thug2_gamespy_check.exe: the GameSpy SDK and its glue driven without the network - its pure helpers,
# the peer's queued callbacks, a NAT negotiation and a qr2 heartbeat and query against fake servers on the loopback
# addresses only - printing what each produced, so that the x86 and x64 builds can be compared byte for byte
# (src/tests/gamespy_check.cpp).
if(TARGET thug2_platform AND THUG2_GAMESPY_SDK)
    add_executable(thug2_gamespy_check gamespy_check.cpp)
    target_link_libraries(thug2_gamespy_check PRIVATE thug2_platform)
endif()

# NOT ORIGINAL - thug2_vulkan_context_check.exe: the Vulkan backend's context tried on this machine - an SDL window with
# a Vulkan surface, the instance (validation when installed), the device, the swapchain, frames cleared and presented
# across a resize, everything released - and what it found printed: the devices, their queue families and the depth
# and texture formats the renderer needs (src/tests/vulkan_context_check.cpp). Needs no Vulkan SDK, only a driver.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_context_check vulkan_context_check.cpp)
    target_link_libraries(thug2_vulkan_context_check PRIVATE thug2_gpu_vulkan)
    # (its own main(): SDL's main wrapper isn't used)
    target_compile_definitions(thug2_vulkan_context_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_ff_shader_check.exe: the Vulkan backend's fixed-function emulation checked - every shader key the
# Direct3D 9 backend's draws can make (a model of src/render_pc's draws), its GLSL generated and compiled, the SPIR-V
# validated by SPIRV-Tools (when built: THUG2_SPIRV_VALIDATOR, cmake/vulkan_libraries.cmake) and handed to the Vulkan
# driver as modules and pipelines (src/tests/ff_shader_check.cpp).
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_ff_shader_check ff_shader_check.cpp ff_shader_check_model.cpp ff_shader_check_gpu.cpp
       )
    target_link_libraries(thug2_ff_shader_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_ff_shader_check PRIVATE SDL_MAIN_HANDLED)
    if(THUG2_SPIRV_VALIDATOR)
        target_link_libraries(thug2_ff_shader_check PRIVATE SPIRV-Tools-static)
        target_compile_definitions(thug2_ff_shader_check PRIVATE THUG2_SPIRV_VALIDATOR)
    endif()
endif()

# NOT ORIGINAL - thug2_vulkan_gpu_check.exe: the Vulkan Gpu's resources tried on this machine through render::Gpu and
# render::GpuContext - every texture format the game makes (BC, the 16-bit formats native and widened, P8), every level,
# buffers static and dynamic, render targets, depth surfaces, copies, read-backs, queries, samplers, the ring, two
# threads at once - each made, filled, read back from the device's memory, compared and released (src/tests/
# vulkan_gpu_check.cpp; --device N picks another GPU).
if(TARGET thug2_gpu_vulkan AND TARGET thug2_fs_win32)
    add_executable(thug2_vulkan_gpu_check vulkan_gpu_check.cpp)
    target_link_libraries(thug2_vulkan_gpu_check PRIVATE thug2_gpu_vulkan thug2_fs_win32 sdl3)
    target_compile_definitions(thug2_vulkan_gpu_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_rt_check.exe: ray tracing's scene on Vulkan (gpu_vulkan/vulkan_rt_scene.h) tried on this
# machine - meshes made through render::Gpu (lists, strips, 16 / 32-bit indices, offsets, a dynamic mesh, many small
# ones), their BLASes, frames of instances, then thousands of rays traced through each frame's TLAS from a compute
# shader and compared with a reference traced on the processor; compaction, refits, removal, an empty scene and the
# validation layer's messages (src/tests/vulkan_rt_check.cpp; --device N picks another GPU; exit 2: no ray tracing).
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_check vulkan_rt_check.cpp)
    target_link_libraries(thug2_vulkan_rt_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_rt_shadow_check.exe: the Vulkan Gpu's ray-traced sun shadows (gpu_vulkan/
# vulkan_rt_shadows.h) on this machine - a small scene drawn through render::Gpu, its acceleration structures built by
# the check, the pass run through rt_scene's listener, the target compared pixel by pixel with a reference worked out on
# the processor (one sample, four, objects casting, soft, a split view, no scene) - src/tests/vulkan_rt_shadow_check.cpp.
# Exit 2 (nothing checked) on a device without ray queries.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_shadow_check vulkan_rt_shadow_check.cpp)
    target_link_libraries(thug2_vulkan_rt_shadow_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_shadow_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_rt_effects_check.exe: the Vulkan Gpu's ray-traced ambient occlusion and its denoiser
# (gpu_vulkan/vulkan_rt_effects.h) on this machine - a small scene drawn through render::Gpu, its acceleration structures
# built by the check; each frame's rays compared with the same directions traced on the processor, the denoised result
# with a many-ray reference after a still camera's frames, and a moved camera's history kept where the surface was seen
# and started again where it wasn't (src/tests/vulkan_rt_effects_check.cpp). Exit 2 on a device without ray queries.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_effects_check vulkan_rt_effects_check.cpp)
    target_link_libraries(thug2_vulkan_rt_effects_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_effects_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_hdr_check.exe: the Vulkan Gpu's HDR targets and tone mapping (gpu_vulkan/vulkan_hdr.h) on
# this machine - float targets filled with synthetic colours (the 16-bit floats 0 - 16, random ones, not-a-number and
# the infinities), the pass run through rt_scene's listener and compared with the processor's curve; identity below the
# knee; a viewport; 8-bit targets left alone; four samples resolved in floats and mapped per sample; the back buffer's
# read-backs and present; .cube files (src/tests/vulkan_hdr_check.cpp). No ray tracing needed.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_hdr_check vulkan_hdr_check.cpp)
    target_link_libraries(thug2_vulkan_hdr_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_hdr_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_rt_gi_check.exe: the Vulkan Gpu's one-bounce ray-traced indirect light
# (gpu_vulkan/vulkan_rt_effects.h, rt_gi) on this machine - a red wall beside a white floor in the sun: each frame's
# light compared with the same rays traced on the processor, the albedo found, the colour it adds, the reddish bounce,
# an open floor left alone and the denoised light against a many-ray reference (src/tests/vulkan_rt_gi_check.cpp). Exit
# 2 on a device without ray queries.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_gi_check vulkan_rt_gi_check.cpp)
    target_link_libraries(thug2_vulkan_rt_gi_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_gi_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_rt_reflections_check.exe: the Vulkan Gpu's ray-traced reflections
# (gpu_vulkan/vulkan_rt_reflections.h) and the scene's geometry table on this machine - texture coordinates and colours
# fetched at rays' hits in every mesh layout compared with the meshes' own, and a mirror floor reflecting a textured box
# compared pixel by pixel with the texel the processor finds (src/tests/vulkan_rt_reflections_check.cpp). Exit 2 on a
# device without ray queries.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_reflections_check vulkan_rt_reflections_check.cpp)
    target_link_libraries(thug2_vulkan_rt_reflections_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_reflections_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_rt_fog_check.exe: the Vulkan Gpu's volumetric fog and light shafts
# (gpu_vulkan/vulkan_rt_fog.h) on this machine - a wall with a window, the sun through it into an even fog; the march
# compared texel by texel with the processor's (the same jitter and steps), the transmittance with the game's linear
# fog, the converged history and the composite with the exact integral, shafts mode, the sky, and the pass skipped
# without fog, a scene or the setting (src/tests/vulkan_rt_fog_check.cpp; --perf: its cost at 1920 x 1080). Exit 2 on a
# device without ray queries.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_fog_check vulkan_rt_fog_check.cpp)
    target_link_libraries(thug2_vulkan_rt_fog_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_fog_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_rt_materials_check.exe: ray tracing's material table and bindless textures
# (gpu_vulkan/vulkan_rt_materials.h) tried on this machine - textures of every format the game makes registered as
# materials, read by a compute shader through the table's GLSL and compared with the texels written; address modes,
# filtering, the alpha test, textures released while used and their slots reused, thousands of materials, textures[]
# at the device's limit (src/tests/vulkan_rt_materials_check.cpp; --device N picks another GPU).
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_materials_check vulkan_rt_materials_check.cpp)
    target_link_libraries(thug2_vulkan_rt_materials_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_materials_check PRIVATE SDL_MAIN_HANDLED)
endif()
