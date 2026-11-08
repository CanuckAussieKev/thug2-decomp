// NOT ORIGINAL - the system's "choose a folder" dialog (folder_dialog.h).

#include "platform/folder_dialog.h"

#include <mutex>
#include <string>

#include <SDL3/SDL.h>

#include "base/core/no_destroy.h"
#include "platform/sdl_window.h"

namespace sys::folder_dialog {

namespace {

// (SDL answers on a thread of its own)
struct DialogState {
    std::mutex lock;
    bool open = false;
    bool answered = false;
    std::string folder;
    std::string why;
};

DialogState& dialog_state()
{
    static core::NoDestroy<DialogState> state;
    return state.get();
}

void answered(void* /*userdata*/, const char* const* files, int /*filter*/)
{
    DialogState& state = dialog_state();
    const std::scoped_lock hold(state.lock);
    state.open = false;
    state.answered = true;
    state.folder.clear();
    state.why.clear();
    if (files == nullptr) {
        state.why = SDL_GetError();
    } else if (*files != nullptr) {
        state.folder = *files;
    }
}

} // namespace

void open(const std::string& start_folder)
{
    DialogState& state = dialog_state();
    {
        const std::scoped_lock hold(state.lock);
        if (state.open) {
            return;
        }
        state.open = true;
        state.answered = false;
    }
    // (the start folder is copied by SDL before it returns)
    SDL_ShowOpenFolderDialog(&answered, nullptr, sdl_window(), start_folder.empty() ? nullptr : start_folder.c_str(), false);
}

bool is_open()
{
    DialogState& state = dialog_state();
    const std::scoped_lock hold(state.lock);
    return state.open;
}

bool take_answer(Answer* answer)
{
    DialogState& state = dialog_state();
    const std::scoped_lock hold(state.lock);
    if (!state.answered) {
        return false;
    }
    state.answered = false;
    answer->folder = state.folder;
    answer->why = state.why;
    return true;
}

} // namespace sys::folder_dialog
