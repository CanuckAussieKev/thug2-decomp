// NOT ORIGINAL - the new front end as an extension (new_frontend_extension.h).

#include "frontend/new_frontend_extension.h"

#include <string>

#include "engine/ext/extension.h"
#include "frontend/new_frontend.h"

namespace frontend::new_frontend {

namespace {

class NewFrontendExtension final : public ext::Extension {
public:
    void register_script_commands() override { register_script_functions(); }
    void prepare_input() override { prepare_all(); }
    // (also while an extension owns the game: it steps aside while the level editor is up)
    void menu_frame(modules::FrontEnd* front_end, bool /*game_owned*/) override { tick_frame(front_end); }

    bool blocks_game_input() override { return new_frontend::blocks_game_input(); }

    void draw_over_frame() override { blit(); }
    void device_lost() override { new_frontend::device_lost(); }
    void before_screen_layout() override { new_frontend::before_screen_layout(); }

    // ([features] new_frontend: the overlay's front_end folder holds its main-menu scripts)
    bool uses_front_end_overlay() override { return is_switched_on(); }

    // (the automation channel's `fe` commands drive its screens)
    bool screen_command(const std::string& line, bool* done, std::string* result) override
    {
        *result = automation_command(line, done);
        return true;
    }
    bool screen_result(std::string* result) override
    {
        *result = automation_result();
        return true;
    }
    void screen_cancel() override { automation_cancel(); }
    bool screen_up(const std::string& name) override { return automation_screen_up(name); }
    bool screen_status(std::string* status) override
    {
        *status = automation_status();
        return true;
    }
};

} // namespace

ext::Extension& extension()
{
    static NewFrontendExtension front_end;
    return front_end;
}

} // namespace frontend::new_frontend
