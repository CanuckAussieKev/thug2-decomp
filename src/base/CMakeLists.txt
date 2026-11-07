# The base layer (src/base): the OS-free helpers every layer uses - checksums, printf-style formatting, text, checked
# indexing, the enum and pointer helpers (base/core), the maths (base/maths), two containers (base/lists), the bit
# stream and BPS patches (base/util). It includes nothing but the standard library and GSL (tools/layers.ts checks
# it); the debugger output and error lines go to sinks the platform installs (base/core/dbg.h, log_sink.h).
add_library(thug2_base STATIC
    core/checksums.cpp
    core/dbg.cpp
    core/format.cpp
    core/log_sink.cpp
    core/single_instance.cpp
    core/str.cpp
    core/stubs.cpp
    maths/fpu_precision.cpp
    maths/mat4.cpp
    maths/maths.cpp
    maths/quat_rotation.cpp
    maths/random.cpp
    maths/rigidbody_math.cpp
    maths/rotation_blend.cpp
    maths/shape_tests.cpp
    maths/vec4.cpp
    maths/vector_rotate_z.cpp
    maths/vector_transform.cpp
    maths/view_transforms.cpp
    util/bitstream.cpp
    util/bps.cpp
)
target_link_libraries(thug2_base PUBLIC thug2_options)
