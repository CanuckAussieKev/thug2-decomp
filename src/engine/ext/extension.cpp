// NOT ORIGINAL - the extension points' registry and dispatch (extension.h).

#include "engine/ext/extension.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"

namespace ext {

namespace {

constexpr size_t kMaxExtensions = 16;

// NOT ORIGINAL grouping - the extensions app/ registered, in their order. Made on first use, never destroyed
// (registry()).
struct Registry {
    std::array<Extension*, kMaxExtensions> extensions_{};
    size_t count_{};
};

Registry& registry()
{
    static core::NoDestroy<Registry> state;
    return state.get();
}

std::span<Extension* const> registered()
{
    return std::span<Extension* const>(registry().extensions_).first(registry().count_);
}

} // namespace

void add(Extension& extension)
{
    // (a 17th ends the game with core::at's report)
    core::at(registry().extensions_, registry().count_++) = &extension;
}

void prepare_input()
{
    for (Extension* extension : registered()) {
        extension->prepare_input();
    }
}

void menu_frame(modules::FrontEnd* front_end)
{
    const bool owned = game_owner() != nullptr;
    for (Extension* extension : registered()) {
        extension->menu_frame(front_end, owned);
    }
    for (Extension* extension : registered()) {
        extension->after_menu_frame(front_end, owned);
    }
}

Extension* game_owner()
{
    for (Extension* extension : registered()) {
        if (extension->owns_game()) {
            return extension;
        }
    }
    return nullptr;
}

bool game_input_blocked()
{
    for (Extension* extension : registered()) {
        if (extension->blocks_game_input()) {
            return true;
        }
    }
    return false;
}

void before_draw_world()
{
    for (Extension* extension : registered()) {
        extension->before_draw_world();
    }
}

void place_camera(gfx::ViewCam* camera, int viewport)
{
    for (Extension* extension : registered()) {
        extension->place_camera(camera, viewport);
    }
}

float near_plane(gfx::ViewCam* camera, float near_z)
{
    for (Extension* extension : registered()) {
        near_z = extension->near_plane(camera, near_z);
    }
    return near_z;
}

float view_angle(gfx::ViewCam* camera, float angle)
{
    for (Extension* extension : registered()) {
        angle = extension->view_angle(camera, angle);
    }
    return angle;
}

void view_drawn(const ViewNote& view)
{
    for (Extension* extension : registered()) {
        extension->view_drawn(view);
    }
}

void draw_over_frame()
{
    for (Extension* extension : registered()) {
        extension->draw_over_frame();
    }
}

void before_present(bool rendering)
{
    for (Extension* extension : registered()) {
        extension->before_present(rendering);
    }
}

bool keeps_every_frame()
{
    for (Extension* extension : registered()) {
        if (extension->keeps_every_frame()) {
            return true;
        }
    }
    return false;
}

void device_lost()
{
    for (Extension* extension : registered()) {
        extension->device_lost();
    }
}

void before_screen_layout()
{
    for (Extension* extension : registered()) {
        extension->before_screen_layout();
    }
}

bool uses_front_end_overlay()
{
    for (Extension* extension : registered()) {
        if (extension->uses_front_end_overlay()) {
            return true;
        }
    }
    return false;
}

bool skips_intro_movies()
{
    for (Extension* extension : registered()) {
        if (extension->skips_intro_movies()) {
            return true;
        }
    }
    return false;
}

void scene_loaded(const char* name, bool is_net)
{
    for (Extension* extension : registered()) {
        extension->scene_loaded(name, is_net);
    }
}

bool plays_edited_copy()
{
    for (Extension* extension : registered()) {
        if (extension->plays_edited_copy()) {
            return true;
        }
    }
    return false;
}

bool added_gaps(const goals::GapTally* checklist, std::vector<std::string>* names,
                std::vector<std::string>* level_names)
{
    for (Extension* extension : registered()) {
        if (extension->added_gaps(checklist, names, level_names)) {
            return true;
        }
    }
    return false;
}

bool* feature_switch(const char* key)
{
    for (Extension* extension : registered()) {
        bool* on = extension->feature_switch(key);
        if (on != nullptr) {
            return on;
        }
    }
    return nullptr;
}

bool draws_unfocused()
{
    for (Extension* extension : registered()) {
        if (extension->draws_unfocused()) {
            return true;
        }
    }
    return false;
}

void open_editor_when_up()
{
    for (Extension* extension : registered()) {
        extension->open_editor_when_up();
    }
}

void editor_opening_frame(modules::FrontEnd* front_end)
{
    for (Extension* extension : registered()) {
        extension->editor_opening_frame(front_end);
    }
}

std::string screen_command(const std::string& line, bool* done)
{
    std::string result;
    for (Extension* extension : registered()) {
        if (extension->screen_command(line, done, &result)) {
            return result;
        }
    }
    *done = true;
    return "error: no screens to drive";
}

std::string screen_result()
{
    std::string result;
    for (Extension* extension : registered()) {
        if (extension->screen_result(&result)) {
            return result;
        }
    }
    return {};
}

void screen_cancel()
{
    for (Extension* extension : registered()) {
        extension->screen_cancel();
    }
}

bool screen_up(const std::string& name)
{
    for (Extension* extension : registered()) {
        if (extension->screen_up(name)) {
            return true;
        }
    }
    return false;
}

std::string screen_status()
{
    std::string status;
    for (Extension* extension : registered()) {
        if (extension->screen_status(&status)) {
            return status;
        }
    }
    return {};
}

bool request_event_shot(const std::string& note)
{
    for (Extension* extension : registered()) {
        if (extension->request_event_shot(note)) {
            return true;
        }
    }
    return false;
}

void show_notice(const std::string& text)
{
    for (Extension* extension : registered()) {
        extension->show_notice(text);
    }
}

bool last_view(std::span<float, 3> position, std::span<float, 3> forward)
{
    for (Extension* extension : registered()) {
        if (extension->last_view(position, forward)) {
            return true;
        }
    }
    return false;
}

std::vector<Description> describe()
{
    std::vector<Description> sections;
    for (Extension* extension : registered()) {
        Description section;
        if (extension->describe(&section)) {
            sections.push_back(std::move(section));
        }
    }
    return sections;
}

bool show_message(const std::string& text, double seconds)
{
    for (Extension* extension : registered()) {
        if (extension->show_message(text, seconds)) {
            return true;
        }
    }
    return false;
}

} // namespace ext
