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
        format_check_demo.cpp
        format_check_surface.cpp)
    target_link_libraries(thug2_format_check PRIVATE thug2_wire)
endif()

# NOT ORIGINAL - thug2_video_check.exe: a video the game wrote (a demo saved as a video, -encode-video) read back with
# the game's own FFmpeg DLLs - codec, size, frame rate, frame count - and frames saved as PNG (src/tests/video_check.cpp).
if(TARGET thug2_platform)
    add_executable(thug2_video_check video_check.cpp "${PROJECT_SOURCE_DIR}/src/media_ffmpeg/ffmpeg_api.cpp"
                                     "${PROJECT_SOURCE_DIR}/src/media_ffmpeg/ffmpeg_video_read.cpp")
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

# NOT ORIGINAL - thug2_daylight_check.exe: the time of day's maths (engine/gfx/rt_daylight.h: the sun's path, each
# level's own day, the light at a time, the original's states blended, the settings' text) checked
# (src/tests/daylight_check.cpp; pure: no game, no device).
if(TARGET thug2_base)
    add_executable(thug2_daylight_check daylight_check.cpp "${PROJECT_SOURCE_DIR}/src/engine/gfx/rt_daylight.cpp"
                                        "${PROJECT_SOURCE_DIR}/src/engine/gfx/rt_sky.cpp")
    target_link_libraries(thug2_daylight_check PRIVATE thug2_base)
endif()

# NOT ORIGINAL - thug2_viewer_check.exe: the demo viewer's camera path (devtools/demo_camera_path.h: the keys' interpolation,
# continuity, the yaw's wrap, the sidecar written and read back) and the split comparison's projection crop
# (engine/gfx/rt_scene.h crop_clip_space) checked (src/tests/viewer_check.cpp).
if(TARGET thug2_base)
    add_executable(thug2_viewer_check viewer_check.cpp "${PROJECT_SOURCE_DIR}/src/devtools/demo_camera_path.cpp")
    target_link_libraries(thug2_viewer_check PRIVATE thug2_base)
endif()

# NOT ORIGINAL - thug2_tess_check.exe: the smoothed curves' tessellation (engine/gfx/smooth_tess.h) on a faceted
# cylinder, a rail and a quarter pipe against their analytic surfaces - no cracks, the cap, displacement; smoothing
# (rt_tess_smooth: prisms rounded, hard edges kept); the water's grid (engine/gfx/water_tess.h) on flat quads; with
# `--level <folder>...` a level's own meshes measured against its collision, offline (src/tests/tess_check.cpp).
if(TARGET thug2_base)
    add_executable(thug2_tess_check tess_check.cpp "${PROJECT_SOURCE_DIR}/src/engine/gfx/smooth_tess.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/engine/gfx/water_tess.cpp")
    target_link_libraries(thug2_tess_check PRIVATE thug2_base)
endif()

# NOT ORIGINAL - thug2_fisheye_check.exe: the fisheye lens' maths (engine/gfx/fisheye_lens.h) - the lens family, the
# game camera's widening and projection, the chain from the widened camera through the lens pass's mapping
# (src/tests/fisheye_check.cpp; pure: no game, no device).
if(TARGET thug2_base)
    add_executable(thug2_fisheye_check fisheye_check.cpp "${PROJECT_SOURCE_DIR}/src/engine/gfx/fisheye_lens.cpp")
    target_link_libraries(thug2_fisheye_check PRIVATE thug2_base)
endif()

# NOT ORIGINAL - thug2_options_check.exe: the new front end's Options screen's tables - every setting in one group (named
# by key), its default among its choices, each choice written to an ini text and read back, the graphics presets
# (src/tests/options_check.cpp; frontend/new_frontend_options.cpp is pure: no game).
if(TARGET thug2_base)
    add_executable(thug2_options_check options_check.cpp "${PROJECT_SOURCE_DIR}/src/frontend/new_frontend_options.cpp")
    target_link_libraries(thug2_options_check PRIVATE thug2_base)
endif()

# NOT ORIGINAL - thug2_frame_pacing_check.exe: [Graphics] max_fps's maths without the game - transforms blended between
# two ticks as rotations, the ends exact, jumps (teleports, camera cuts) found; the render-only frames' schedule
# (src/tests/frame_pacing_check.cpp; base/maths/transform_blend.cpp, engine/sys/frame_pacing.cpp are pure).
if(TARGET thug2_base)
    add_executable(thug2_frame_pacing_check frame_pacing_check.cpp "${PROJECT_SOURCE_DIR}/src/engine/sys/frame_pacing.cpp"
        "${PROJECT_SOURCE_DIR}/src/engine/sys/tick_rate.cpp")
    target_link_libraries(thug2_frame_pacing_check PRIVATE thug2_base)
endif()

# NOT ORIGINAL - thug2_tick_rate_check.exe: [Graphics] tick_rate's maths without the game - at 60 every conversion is
# the original's to the bit, other rates scale, and the exactness yardstick: the game's integration kernels stepped at 60
# and 120 / 144 / 240 (src/tests/tick_rate_check.cpp; engine/sys/tick_rate.cpp, frame_pacing.cpp are pure).
if(TARGET thug2_base)
    add_executable(thug2_tick_rate_check tick_rate_check.cpp "${PROJECT_SOURCE_DIR}/src/engine/sys/tick_rate.cpp"
        "${PROJECT_SOURCE_DIR}/src/engine/sys/frame_pacing.cpp")
    target_link_libraries(thug2_tick_rate_check PRIVATE thug2_base)
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

# NOT ORIGINAL - thug2_vulkan_pbr_check.exe: the Vulkan Gpu's per-pixel lighting and surface maps (gpu_vulkan/
# vulkan_surface.h, [Graphics] rt_pbr) on this machine - quads drawn with the game's material shader and a synthetic
# normal / roughness map into a float target, each pixel against the same light worked out on the processor: lit and
# baked, turned, mirrored and rotated texture coordinates, no normals, no map, the setting off (the original's shader),
# no NaN (src/tests/vulkan_pbr_check.cpp). No ray tracing needed.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_pbr_check vulkan_pbr_check.cpp)
    target_link_libraries(thug2_vulkan_pbr_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_pbr_check PRIVATE SDL_MAIN_HANDLED)
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

# NOT ORIGINAL - thug2_vulkan_style_check.exe: the art style's toon filter (gpu_vulkan/vulkan_style.h) on this machine -
# synthetic pictures and depths (a box, a tilted floor, a ridge, the sky; noise, a gradient, not-a-number) filtered
# through rt_scene's listener and compared with the processor's twin; the smoothing, the bands and the outlines where
# they belong; a viewport (outside untouched), the style off and strength 0 (no pass), an 8-bit and a multisampled
# target; the cost at 2560x1440 (src/tests/vulkan_style_check.cpp). No ray tracing needed.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_style_check vulkan_style_check.cpp)
    target_link_libraries(thug2_vulkan_style_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_style_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_grass_check.exe: the grass (engine/gfx/grass.h's field, gpu_vulkan/vulkan_grass.h's pass)
# on this machine - a lawn with a box on it and a ground of two colours: the field's blades only on the grass faces,
# none under the box, the same field and picture every time, the blades only over the lawn and in the ground's colours,
# the device's blade shapes against the processor's twin, the level-of-detail fade, the push away from a skater, no NaN,
# a multisampled target, validation; the cost at 2560x1440 (src/tests/vulkan_grass_check.cpp). No ray tracing needed.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_grass_check vulkan_grass_check.cpp)
    target_link_libraries(thug2_vulkan_grass_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_grass_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_scatter_check.exe: the decals and clutter (engine/gfx/scatter.h's placement,
# gpu_vulkan/vulkan_scatter.h's passes) on this machine - a synthetic street's collision: the same placement every time,
# each decal's kind its surface's, every vertex on its face and none past an edge, nothing on the skate lines (a quarter
# pipe, a rail, a ledge's top, steps), clutter only near kerbs and walls; the passes over a drawn road: marks only darker,
# only in decals' footprints, none behind a wall or over nothing; items only at the kerb's foot; four samples; off as
# before; validation; the cost at 2560x1440 (src/tests/vulkan_scatter_check.cpp). No ray tracing needed.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_scatter_check vulkan_scatter_check.cpp)
    target_link_libraries(thug2_vulkan_scatter_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_scatter_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_cel_check.exe: cel shading (engine/gfx/rt_cel.h, gpu_vulkan/vulkan_cel.h) on this machine
# - the bands of a lit gradient (the relit stream, 2 - 4 bands; the bake without it; an object lit by a light, its rim),
# the shadow band's tint, hard ray-traced shadows (with ray tracing), a lit cube's hull and the world's lines, flatter
# textures, rt_cel=0 the original's bytes; the cost at 2560x1440 (src/tests/vulkan_cel_check.cpp).
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_cel_check vulkan_cel_check.cpp)
    target_link_libraries(thug2_vulkan_cel_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_cel_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_water_check.exe: water shading ([Graphics] rt_water, gpu_vulkan/vulkan_water.h) on this
# machine - a synthetic shore drawn through render::Gpu: calm water against the processor (refraction, absorption, the
# deep colour, Fresnel), rt_water off and an unmarked draw byte for byte the original's, foam only where shallow, waves
# that move with the game's clock and repeat with it, the sun's glitter above 1, the ripple texture against its twin
# (src/tests/vulkan_water_check.cpp). No ray tracing needed.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_water_check vulkan_water_check.cpp)
    target_link_libraries(thug2_vulkan_water_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_water_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_camera_fx_check.exe: the camera effects (gpu_vulkan/vulkan_camera_fx.h) on this machine -
# everything off (no listener, no motion vectors, the picture unchanged); each grade look against the processor's twin;
# the depth of field sharp at the focus and blurred away from it, near and far; the motion blur along a known camera
# move, none across a cut or a jump; the cost at 2560x1440 (src/tests/vulkan_camera_fx_check.cpp). No ray tracing needed.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_camera_fx_check vulkan_camera_fx_check.cpp)
    target_link_libraries(thug2_vulkan_camera_fx_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_camera_fx_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_style_film_check.exe: the vhs, retro and watercolour art styles
# (gpu_vulkan/vulkan_style_film.h) on this machine - a synthetic picture (noise, a gradient, not-a-number) filtered
# through rt_scene's listener and compared with each style's processor twin; the vhs picture following the game clock,
# the retro cells and palette, the watercolour's calm; a viewport (outside untouched), strength 0, an 8-bit and a
# multisampled target; the retro geometry (snapped corners, affine colours) on a 3D quad drawn through the
# fixed-function shaders; the cost at 2560x1440 (src/tests/vulkan_style_film_check.cpp). No ray tracing needed.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_style_film_check vulkan_style_film_check.cpp)
    target_link_libraries(thug2_vulkan_style_film_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_style_film_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_upscale_check.exe: the Vulkan Gpu's upscaling (gpu_vulkan/vulkan_upscale.h) and its motion
# vectors (gpu_vulkan/vulkan_motion.h) on this machine - the jitter sequence; every clear-cut pixel's motion of a still
# ground, a moving quad, a skinned quad and the sky against the processor's reprojection; FSR (or DLSS with --dlss) on a
# still scene of thin bars against a 4x4 supersampled reference, the jitter's sign, no NaN, the same bytes twice
# (src/tests/vulkan_upscale_check.cpp). The upscalers' DLLs are copied beside it. Exit 2 when no upscaler runs here.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_upscale_check vulkan_upscale_check.cpp)
    target_link_libraries(thug2_vulkan_upscale_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_upscale_check PRIVATE SDL_MAIN_HANDLED)
    if(THUG2_UPSCALER_DLLS)
        add_custom_command(TARGET thug2_vulkan_upscale_check POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THUG2_UPSCALER_DLLS} "$<TARGET_FILE_DIR:thug2_vulkan_upscale_check>"
            COMMENT "Copying the upscalers' DLLs next to thug2_vulkan_upscale_check.exe")
    endif()
endif()

# NOT ORIGINAL - thug2_vulkan_framegen_check.exe: frame generation and Reflex (gpu_vulkan/vulkan_framegen.h) on this
# machine - off (no provider, no feature, Vulkan's own swapchain), DLSS-G (an NVIDIA RTX 40 / 50) and FSR 3 (its own
# swapchain): the feature made and released, frames on while moving, more frames shown than drawn, off under a menu, a
# cut resetting the history (src/tests/vulkan_framegen_check.cpp). The DLLs are copied beside it. Exit 2 when neither
# provider runs here.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_framegen_check vulkan_framegen_check.cpp)
    target_link_libraries(thug2_vulkan_framegen_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_framegen_check PRIVATE SDL_MAIN_HANDLED)
    if(THUG2_UPSCALER_DLLS)
        add_custom_command(TARGET thug2_vulkan_framegen_check POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THUG2_UPSCALER_DLLS} "$<TARGET_FILE_DIR:thug2_vulkan_framegen_check>"
            COMMENT "Copying the upscalers' and frame generation's DLLs next to thug2_vulkan_framegen_check.exe")
    endif()
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

# NOT ORIGINAL - thug2_vulkan_rt_lights_check.exe: the night lighting (gpu_vulkan/vulkan_rt_lights.h, rt_lights) on this
# machine - the light sources' clustering and selection on the processor, then a lamp beside a box on a dark floor: each
# pixel's lamp light and the box's shadow against the processor's, four samples, a red-textured glow, two lamps with one
# traced, off and a lamp out of view leaving every pixel as drawn (src/tests/vulkan_rt_lights_check.cpp). Exit 2 on a
# device without ray queries.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_lights_check vulkan_rt_lights_check.cpp)
    target_link_libraries(thug2_vulkan_rt_lights_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_lights_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_rt_relit_check.exe: the relit mode's analysis (engine/gfx/rt_relit.h) on this machine - a
# small level whose bake is made on the processor from known tints, a known sun and sky and a lamp (the same rays,
# traced by a processor's ray tracer), taken apart through the Vulkan RayTracing: the traced light against the
# processor's, the model, the tints, the lamp's residual, relit at the bake's sun and at another against the processor's
# bakes, a sunless level, the per-level list (src/tests/vulkan_rt_relit_check.cpp; --perf: a level of the game's size).
# Exit 2 on a device without ray queries.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_relit_check vulkan_rt_relit_check.cpp)
    target_link_libraries(thug2_vulkan_rt_relit_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_relit_check PRIVATE SDL_MAIN_HANDLED)
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

# NOT ORIGINAL - thug2_vulkan_rt_sun_check.exe: the Vulkan Gpu's sun, its glare, the screen-space rays, the bloom and
# the sky model (gpu_vulkan/vulkan_rt_sun.h, vulkan_rt_sky.h; engine/gfx/rt_sky.h) on this machine - the sky's table
# against the model's integrals, the disc and glare against their formula, the rays against the processor's march,
# the bloom keeping the light it spreads (8-bit and 16-bit float targets), the aerial haze's transmittance against its
# closed form (src/tests/vulkan_rt_sun_check.cpp; --perf: the cost at 1920 x 1080). Exit 2 on a device without ray
# queries.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_sun_check vulkan_rt_sun_check.cpp)
    target_link_libraries(thug2_vulkan_rt_sun_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_sun_check PRIVATE SDL_MAIN_HANDLED)
endif()

# NOT ORIGINAL - thug2_vulkan_rt_sky_check.exe: the Vulkan Gpu's sky - the atmosphere's tables against rt_sky.h's
# multiple-scattering integrals, the level's sky analysed and matched, the composite against the sky-view table, the
# clouds' noise and march against engine/gfx/rt_clouds.h's processor twins, their temporal pass converging without
# ghosts (src/tests/vulkan_rt_sky_check.cpp; --perf: the cost at 1920 x 1080). No ray tracing needed.
if(TARGET thug2_gpu_vulkan)
    add_executable(thug2_vulkan_rt_sky_check vulkan_rt_sky_check.cpp)
    target_link_libraries(thug2_vulkan_rt_sky_check PRIVATE thug2_gpu_vulkan sdl3)
    target_compile_definitions(thug2_vulkan_rt_sky_check PRIVATE SDL_MAIN_HANDLED)
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
