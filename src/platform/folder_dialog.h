#pragma once

#include <string>

#include "base/core/api.h"

// NOT ORIGINAL - the system's "choose a folder" dialog (ours: the level editor's Import Level window - SDL 3's folder
// dialog, over the game's window). It doesn't hold the game up: open asks for it, and the frames go on while it is
// up; take_answer gives the answer once (on the frame after the user closed it).
namespace sys::folder_dialog {

// Asks for the dialog, starting in `start_folder` (UTF-8; empty: the system's choice). Nothing while one is up.
THUG2_API void open(const std::string& start_folder);
// Whether one is up.
THUG2_API bool is_open();
// The answer, once: true with the folder chosen (UTF-8), or with `folder` empty when the user cancelled (or the
// dialog couldn't be shown - `why` then says so); false while there is none.
struct Answer {
    std::string folder;
    std::string why;
};
THUG2_API bool take_answer(Answer* answer);

} // namespace sys::folder_dialog
