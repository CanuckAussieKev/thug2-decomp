// NOT ORIGINAL - the new front end as an extension (new_frontend_extension.h).

#include "frontend/new_frontend_extension.h"

#include "engine/ext/extension.h"
#include "frontend/new_frontend.h"

namespace frontend::new_frontend {

namespace {

class NewFrontendExtension final : public ext::Extension {
public:
    void register_script_commands() override { register_script_functions(); }
    void prepare_input() override { prepare_all(); }

    bool blocks_game_input() override { return new_frontend::blocks_game_input(); }

    void draw_over_frame() override { blit(); }
    void device_lost() override { new_frontend::device_lost(); }
    void before_screen_layout() override { new_frontend::before_screen_layout(); }

    // ([features] new_frontend: the overlay's front_end folder holds its main-menu scripts)
    bool uses_front_end_overlay() override { return is_switched_on(); }
};

} // namespace

ext::Extension& extension()
{
    static NewFrontendExtension front_end;
    return front_end;
}

} // namespace frontend::new_frontend
