#pragma once

// The instance renderer's shared state (see pc_instance.cpp).

namespace maths {
class Mat4;
}

namespace render {

const maths::Mat4* last_bones();
void set_last_bones(const maths::Mat4* value);

} // namespace render
