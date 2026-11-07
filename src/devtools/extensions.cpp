// NOT ORIGINAL - the dev tools as extensions (extensions.h).

#include "devtools/extensions.h"

#include <cstring>
#include <span>
#include <string>

#include "devtools/automation.h"
#include "devtools/restart_tester.h"
#include "devtools/screenshot.h"
#include "game/rider/test_view.h"
#include "engine/ext/extension.h"
#include "engine/sys/editor_mode.h"

namespace frontend::restart_tester {
namespace {
class RestartTesterExtension final : public ext::Extension {
public:
    void menu_frame(modules::FrontEnd* front_end, bool game_owned) override
    {
        if (!game_owned) {
            tick_frame(front_end);
        }
    }
    bool* feature_switch(const char* key) override
    {
        return std::strcmp(key, "restart_tester") == 0 ? &is_switched_on() : nullptr;
    }
};
} // namespace

ext::Extension& extension()
{
    static RestartTesterExtension restart_tester;
    return restart_tester;
}
} // namespace frontend::restart_tester

namespace frontend::automation {
namespace {
class AutomationExtension final : public ext::Extension {
public:
    // (the editor mode: also while the level editor owns the game - its window is the application, the tests drive it
    // with `fe editor ...` - engine/sys/editor_mode.h)
    void menu_frame(modules::FrontEnd* front_end, bool game_owned) override
    {
        if (!game_owned || sys::editor_mode::on()) {
            tick_frame(front_end);
        }
    }
    // (a test's picture of the window - PrintWindow - shows the frame now, not the last one drawn before the window lost
    // the focus)
    bool draws_unfocused() override { return is_switched_on(); }
};
} // namespace

ext::Extension& extension()
{
    static AutomationExtension automation;
    return automation;
}
} // namespace frontend::automation

namespace frontend::test_view {
namespace {
class TestViewExtension final : public ext::Extension {
public:
    void register_script_commands() override { register_script_functions(); }
    void place_camera(gfx::ViewCam* camera, int viewport) override { test_view::place_camera(camera, viewport); }
};
} // namespace

ext::Extension& extension()
{
    static TestViewExtension test_view;
    return test_view;
}
} // namespace frontend::test_view

namespace sys::screenshot {
namespace {
class ScreenshotExtension final : public ext::Extension {
public:
    void prepare_input() override { prepare_all(); }
    void view_drawn(const ext::ViewNote& view) override { note_view(view.viewport, view.position, view.forward); }
    void before_present(bool rendering) override { screenshot::before_present(rendering); }
    bool keeps_every_frame() override { return series_running(); }
    bool request_event_shot(const std::string& note) override { return screenshot::request_event_shot(note); }
    void show_notice(const std::string& text) override { screenshot::show_notice(text); }
    bool last_view(std::span<float, 3> position, std::span<float, 3> forward) override
    {
        return screenshot::last_view(position, forward);
    }
};
} // namespace

ext::Extension& extension()
{
    static ScreenshotExtension screenshot;
    return screenshot;
}
} // namespace sys::screenshot
