#pragma once

// The instance renderer's shared state (see pc_instance.cpp).

#include "hook/game_global.h"

namespace Mth {
class Matrix;
}

namespace Nx {

GAME_GLOBAL_EXTERN(const Mth::Matrix*, g_lastBones); // the bone matrices last set up

} // namespace Nx
