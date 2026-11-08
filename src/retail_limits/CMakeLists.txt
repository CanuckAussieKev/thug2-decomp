# The retail-limits DLL (src/retail_limits, docs/retail-limits.md): dinput8.dll for the original game's folder, raising
# its limits (script pools, heaps, the node and rail name tables) at run time for large levels. No layer of the game -
# it links none of its libraries (tools/layers.json retail_limits) - and 32-bit only: the original game is. The C
# runtime is linked in (/MT): the original game's folder has no runtime DLLs of ours.
#   thug2_retail_limits        the DLL's code (a static library)
#   thug2_retail_limits_dll    dinput8.dll
#   thug2_retail_limits_check  thug2-limits-check.exe: its checks against exe files, without the game running
add_library(thug2_retail_limits STATIC
    exe_image.cpp
    log.cpp
    node_hash.cpp
    patches.cpp
    settings.cpp
)
target_link_libraries(thug2_retail_limits PUBLIC thug2_options)

add_library(thug2_retail_limits_dll SHARED
    dllmain.cpp
    dinput8.def
)
target_link_libraries(thug2_retail_limits_dll PRIVATE thug2_retail_limits)
# Windows loads it from the game's folder in place of the system's because of its name.
set_target_properties(thug2_retail_limits_dll PROPERTIES OUTPUT_NAME dinput8)

add_executable(thug2_retail_limits_check check_main.cpp)
target_link_libraries(thug2_retail_limits_check PRIVATE thug2_retail_limits)
set_target_properties(thug2_retail_limits_check PROPERTIES OUTPUT_NAME thug2-limits-check)

foreach(target IN ITEMS thug2_retail_limits thug2_retail_limits_dll thug2_retail_limits_check)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # (written next to each other in their own folder: dinput8.dll at the top of the build folder would be loaded by
    # the build's own programs in place of Windows' DirectInput)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/retail_limits")
endforeach()
