#pragma once

// The instance renderer's shared state (see pc_instance.cpp).

namespace maths {
class Mat4;
}

namespace render {

extern const maths::Mat4* g_last_bones; // the bone matrices last set up

} // namespace render
