#pragma once

// The instance renderer's shared state (see pc_instance.cpp).

namespace Maths {
class Mat4;
}

namespace Engine3D {

extern const Maths::Mat4* g_lastBones; // the bone matrices last set up

} // namespace Nx
