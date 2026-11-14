# The file formats: readers and writers of the game's data files, saves and custom parks (round-trip checked by
# thug2_format_check).
add_library(thug2_formats STATIC
    anim_format.cpp
    bundle_format.cpp
    collision_format.cpp
    demo_format.cpp
    font_format.cpp
    image_format.cpp
    lzss_unpack.cpp
    park_map_format.cpp
    pre_format.cpp
    rail_buffer_format.cpp
    save_file_format.cpp
    skeleton_format.cpp
    detail_material_format.cpp
    surface_map_format.cpp
    texture_format.cpp
    wave_format.cpp
)
target_link_libraries(thug2_formats PUBLIC thug2_base)
