#pragma once

// The instance renderer's shared state (see pc_instance.cpp).

namespace Mth {
class Matrix;
}

namespace Nx {

extern const Mth::Matrix* g_lastBones; // the bone matrices last set up

} // namespace Nx
