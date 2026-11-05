# The dev tools (src/devtools): the test-automation channel, the test view, the restart tester, starting in a level and
# the screenshot key - extensions the app registers (ext::Extension). They include the game and below (tools/layers.ts
# checks it), plus the front end's and the editor's headers until the editor / front end API
# (tools/layers_baseline.txt).
add_library(thug2_devtools STATIC
    restart_tester.cpp
    screenshot.cpp
    extensions.cpp
    test_view.cpp
    start_level.cpp
    automation.cpp
    automation_report.cpp
    automation_parse.cpp
)
# (the generated build_info.h: the screenshot sidecar names the exe's source)
target_include_directories(thug2_devtools PRIVATE "${CMAKE_BINARY_DIR}/generated")
add_dependencies(thug2_devtools build_info)
target_link_libraries(thug2_devtools PUBLIC thug2_game)
# The sibling edges tools/layers_baseline.txt lists (devtools -> frontend, editor).
target_link_libraries(thug2_devtools PRIVATE thug2_frontend thug2_editor)
