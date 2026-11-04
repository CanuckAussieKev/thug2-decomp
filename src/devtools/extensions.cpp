// NOT ORIGINAL - the dev tools as extensions (extensions.h).

#include "devtools/extensions.h"

#include <cstdint>
#include <span>
#include <string>

#include "devtools/automation.h"
#include "devtools/restart_tester.h"
#include "devtools/screenshot.h"
#include "devtools/start_level.h"
#include "devtools/test_view.h"
#include "engine/ext/extension.h"

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
};
} // namespace

ext::Extension& extension()
{
    static RestartTesterExtension restart_tester;
    return restart_tester;
}
} // namespace frontend::restart_tester

namespace frontend::begin_level {
namespace {
class StartLevelExtension final : public ext::Extension {
public:
    void menu_frame(modules::FrontEnd* front_end, bool game_owned) override
    {
        if (!game_owned) {
            tick_frame(front_end);
        }
    }
    bool skips_intro_movies() override { return edit_requested(); }
};
} // namespace

ext::Extension& extension()
{
    static StartLevelExtension start_level;
    return start_level;
}
} // namespace frontend::begin_level

namespace frontend::automation {
namespace {
class AutomationExtension final : public ext::Extension {
public:
    void menu_frame(modules::FrontEnd* front_end, bool game_owned) override
    {
        if (!game_owned) {
            tick_frame(front_end);
        }
    }
    void add_pad_buttons(int port, std::span<uint8_t> data) override { automation::add_pad_buttons(port, data); }
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
