#pragma once

// The vtable every Tsk::Task<T> instance of the game's managers shares (the compiler folded the identical ones;
// slot 0 is the skate module's task code). Defined in net_app.cpp; `kInputHandlerVtable`, the park editor's and the
// viewer's input handlers' (the other folded table), in park_editor_state.cpp.

namespace GameCode {

extern void** const kTaskVtable;
extern void** const kInputHandlerVtable;

} // namespace GameCode
