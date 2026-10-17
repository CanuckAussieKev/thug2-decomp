#pragma once

// The park editor's manager and cursor: the parts at 0x0059C900-0x0059E0F7 (park_editor.cpp, park_cursor.cpp).
// The types are the park editor's one set (Ed::CParkManager, Ed::CCursor, GridDims, GapInfo, CPieceSet...) in
// game/ed/park.h; this code still reaches some of their fields through the offset accessors there.

#include "game/ed/park.h"
