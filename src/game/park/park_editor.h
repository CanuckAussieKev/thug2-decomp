#pragma once

// The park editor's manager and cursor: the parts at 0x0059C900-0x0059E0F7 (park_editor.cpp, park_cursor.cpp).
// The types are the park editor's one set (ParkEditing::CParkLayout, ParkEditing::CBuildPointer, SCellBox, SGapDetails, CPieceFamily...) in
// game/park_editing/park.h; this code still reaches some of their fields through the offset accessors there.

#include "game/park_editing/park.h"
