# The new front end (src/frontend): the game's own menus restyled, drawn with Dear ImGui, and the mouse control of
# the menus - an extension the app registers (ext::Extension). It includes the game and below (tools/layers.ts checks
# it), plus - until the editor / front end API (tools/layers_baseline.txt) - the editor's and the dev tools' headers:
# those links are the declared sibling edges below.
add_library(thug2_frontend STATIC
    mouse_menus.cpp
    new_frontend.cpp
    new_frontend_automation.cpp
    new_frontend_input.cpp
    new_frontend_widgets.cpp
    new_frontend_data.cpp
    new_frontend_extension.cpp
    new_frontend_draw.cpp
    new_frontend_game.cpp
    new_frontend_mirror.cpp
    new_frontend_model.cpp
    new_frontend_options.cpp
)
target_link_libraries(thug2_frontend PUBLIC thug2_game PRIVATE imgui)
# The sibling edges tools/layers_baseline.txt lists (frontend -> editor, devtools): static libraries that need each
# other, which CMake repeats on the link line for linkers that read the libraries once (GNU ld).
target_link_libraries(thug2_frontend PRIVATE thug2_editor thug2_devtools)
