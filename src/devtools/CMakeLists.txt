# The dev tools (src/devtools): the test-automation channel, the restart tester, the screenshot key and the test views'
# extension - extensions the app registers (ext::Extension). They include the game and below (tools/layers.ts checks
# it), never the editor or the front end.
add_library(thug2_devtools STATIC
    restart_tester.cpp
    screenshot.cpp
    extensions.cpp
    automation.cpp
    automation_commands.cpp
    automation_trace.cpp
    automation_report.cpp
    automation_parse.cpp
    console.cpp
    demo.cpp
    demo_overlay.cpp
    rt_check.cpp
)
# (the generated build_info.h: the screenshot sidecar names the exe's source)
target_include_directories(thug2_devtools PRIVATE "${CMAKE_BINARY_DIR}/generated")
add_dependencies(thug2_devtools build_info)
target_link_libraries(thug2_devtools PUBLIC thug2_game PRIVATE imgui)
