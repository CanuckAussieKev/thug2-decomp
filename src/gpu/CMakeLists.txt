# GPU vertex data (src/gpu): the vertex and index buffers - the Direct3D 9 buffers, made on the device the renderer
# hands it (render::set_buffer_device). It includes only base and platform (tools/layers.ts checks it).
add_library(thug2_gpu STATIC
    vertex_buffers.cpp
)
target_link_libraries(thug2_gpu PUBLIC thug2_platform)
