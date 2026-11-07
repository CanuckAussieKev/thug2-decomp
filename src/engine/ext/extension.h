#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gfx {
class ViewCam;
}
namespace modules {
struct FrontEnd;
}
namespace goals {
class GapTally;
}

// NOT ORIGINAL - the extension points: where the engine and the game call what we added on top of the original (the
// level editor, the new front end and the mouse menus, the dev tools, our NOT ORIGINAL game features that run at the
// same places). The original code calls ext:: at a fixed place in its frame or at an event; ext:: calls the
// extensions that app/ registered at start-up, in their order (the registration order is the call order at every
// point - app/extensions.cpp). Nothing here knows which extensions there are: the exe registers the new front end,
// the dev tools and our game features, and the level editor's from its plugin (plugin.h).
//
// The grain is coarse: a few calls per frame, per viewport or per event, never per object. Every method has a default
// that does nothing (or passes its value through), so an extension overrides only the points it uses.
namespace ext {

// What the 3D frame noted of a viewport's camera once the world's projection was chosen (render::draw_viewport).
struct ViewNote {
    gfx::ViewCam* camera = nullptr;
    int viewport = 0;
    float angle = 0.0F;  // the field of view used for the world, degrees
    float aspect = 0.0F; // width over height
    std::array<uint32_t, 4> rect = {{0U, 0U, 0U, 0U}};      // the device viewport: x, y, width, height
    std::array<float, 3> position = {{0.0F, 0.0F, 0.0F}};    // the camera's
    std::array<float, 3> forward = {{0.0F, 0.0F, 0.0F}};     // where it looks (its matrix's -z row)
};

// A section of the screenshot key's sidecar that an extension adds (title, then key / value lines).
struct Description {
    std::string title;
    std::vector<std::pair<std::string, std::string>> fields;
};

class Extension {
public:
    Extension() = default;
    Extension(const Extension&) = delete;
    Extension& operator=(const Extension&) = delete;
    Extension(Extension&&) = delete;
    Extension& operator=(Extension&&) = delete;

    // --- start-up ---
    // Its script commands (app's start-up calls each extension's at its own place in the order of the added commands).
    virtual void register_script_commands() {}
    // os_layer::init_input, after the input sources started (SDL's own event handler first): its event handlers
    // registered, its keys read.
    virtual void prepare_input() {}

    // --- the front end's input logic (modules::FrontEnd::input_logic, once a frame with the first controller) ---
    // `game_owned`: an extension owns the game this frame (owns_game, asked once before the first call).
    virtual void menu_frame(modules::FrontEnd* /*front_end*/, bool /*game_owned*/) {}
    // After every extension's menu_frame (what must see what the others did this frame: who has the mouse).
    virtual void after_menu_frame(modules::FrontEnd* /*front_end*/, bool /*game_owned*/) {}

    // --- input ---
    // While true the extension owns the game: the game is paused under it, its tools that read keys stand aside, and
    // the keyboard gives the game only the Pause key - as pass_pause_key says (asked only of the owner).
    virtual bool owns_game() { return false; }
    virtual bool pass_pause_key(bool down) { return down; }
    // While true the game reads no key or controller (the extension has them all).
    virtual bool blocks_game_input() { return false; }

    // --- the frame ---
    // render::draw_world, first: its own frame (it may run while the game is paused).
    virtual void before_draw_world() {}
    // render::draw_viewport, before the viewport is set up: the camera may be put elsewhere.
    virtual void place_camera(gfx::ViewCam* /*camera*/, int /*viewport*/) {}
    // The near plane for the world's passes (the sky's keeps the original's) and the field of view of every pass.
    virtual float near_plane(gfx::ViewCam* /*camera*/, float near_z) { return near_z; }
    virtual float view_angle(gfx::ViewCam* /*camera*/, float angle) { return angle; }
    // The world's projection chosen for a viewport.
    virtual void view_drawn(const ViewNote& /*view*/) {}
    // render::draw_world, after the 2D pass and its black bars: drawn over everything.
    virtual void draw_over_frame() {}
    // The main loop, just before the scene is ended and presented (`rendering`: this frame was drawn).
    virtual void before_present(bool /*rendering*/) {}
    // While true the main loop doesn't skip frames to catch up (every logic frame is drawn).
    virtual bool keeps_every_frame() { return false; }
    // The device is about to be reset: its default-pool objects dropped (made again on its next frame).
    virtual void device_lost() {}
    // The screen elements' layout pass is about to run (the original's elements may be hidden or shown).
    virtual void before_screen_layout() {}

    // --- data and levels ---
    // The data overlay's front_end folder (the new front end's scripts) searched first.
    virtual bool uses_front_end_overlay() { return false; }
    // The start-up's logo and intro movies skipped (read once, at the first movie).
    virtual bool skips_intro_movies() { return false; }
    // A level's scene file loaded (render::load_scene, once the scene is up).
    virtual void scene_loaded(const char* /*name*/, bool /*is_net*/) {}
    // The level played is an edited copy: its records and high scores are kept apart from the level's.
    virtual bool plays_edited_copy() { return false; }
    // The career save: the names of the gaps the extension put in `checklist` and the level's own gap names (false,
    // nothing: none of its gaps are in that checklist).
    virtual bool added_gaps(const goals::GapTally* /*checklist*/, std::vector<std::string>* /*names*/,
                            std::vector<std::string>* /*level_names*/)
    {
        return false;
    }

    // --- settings ---
    // Its feature switch `key` (thug2-decomp.ini [features] <key>, read once; the options screen sets it live), or none
    // (not its).
    virtual bool* feature_switch(const char* /*key*/) { return nullptr; }
    // While true the game keeps drawing while its window is in the background (a test's picture of the window shows the
    // frame now).
    virtual bool draws_unfocused() { return false; }

    // --- the editor opened in a level (-edit / edit_level: game/front_end/start_level.h) ---
    // The level's load began, and the editor is to open there once it is up.
    virtual void open_editor_when_up() {}
    // Once a frame after that, from start level's frame (the level loading, then up): the editor's own steps to open.
    virtual void editor_opening_frame(modules::FrontEnd* /*front_end*/) {}

    // --- the screens a test drives (the test-automation channel's `fe` commands) ---
    // A command for the screens: false when not its; `result` and `done` as the automation channel reads them.
    virtual bool screen_command(const std::string& /*line*/, bool* /*done*/, std::string* /*result*/) { return false; }
    // The result of a command still waiting ("" while it waits).
    virtual bool screen_result(std::string* /*result*/) { return false; }
    // The command waiting given up.
    virtual void screen_cancel() {}
    // Whether the screen `name` is shown and not moving.
    virtual bool screen_up(const std::string& /*name*/) { return false; }
    // Its line of the automation channel's status file ("" when off).
    virtual bool screen_status(std::string* /*status*/) { return false; }

    // --- what a dev tool offers the game's own debugging aids, and what it asks of the others ---
    // A picture of the frame being drawn, `note` in its sidecar (false: dropped - none taken).
    virtual bool request_event_shot(const std::string& /*note*/) { return false; }
    // A notice on screen for a moment.
    virtual void show_notice(const std::string& /*text*/) {}
    // The last camera noted (false: none lately).
    virtual bool last_view(std::span<float, 3> /*position*/, std::span<float, 3> /*forward*/) { return false; }
    // Its section of the screenshot's sidecar (false: none now).
    virtual bool describe(Description* /*out*/) { return false; }
    // A message shown by the extension that has the screen (false: none has - the caller shows it).
    virtual bool show_message(const std::string& /*text*/, double /*seconds*/) { return false; }

protected:
    ~Extension() = default;
};

// app/'s start-up: an extension added after the others (before anything below is called).
void add(Extension& extension);

// The dispatch: each calls the registered extensions in their order.
void prepare_input();
// The front end's input logic, once a frame: owns_game asked once, then every menu_frame, then every
// after_menu_frame.
void menu_frame(modules::FrontEnd* front_end);
// The extension that owns the game (the first whose owns_game is true), or none.
Extension* game_owner();
bool game_input_blocked();
void before_draw_world();
void place_camera(gfx::ViewCam* camera, int viewport);
// Each extension's near_plane / view_angle in turn, starting from the original's value.
float near_plane(gfx::ViewCam* camera, float near_z);
float view_angle(gfx::ViewCam* camera, float angle);
void view_drawn(const ViewNote& view);
void draw_over_frame();
void before_present(bool rendering);
bool keeps_every_frame();
void device_lost();
void before_screen_layout();
bool uses_front_end_overlay();
bool skips_intro_movies();
void scene_loaded(const char* name, bool is_net);
bool plays_edited_copy();
bool added_gaps(const goals::GapTally* checklist, std::vector<std::string>* names,
                std::vector<std::string>* level_names);
// The first extension's switch `key` (null: none has it).
bool* feature_switch(const char* key);
bool draws_unfocused();
void open_editor_when_up();
void editor_opening_frame(modules::FrontEnd* front_end);
// The first extension that takes it; with none: "error: no screens to drive" (done), "", nothing, false, "".
std::string screen_command(const std::string& line, bool* done);
std::string screen_result();
void screen_cancel();
bool screen_up(const std::string& name);
std::string screen_status();
// (the first extension that takes it)
bool request_event_shot(const std::string& note);
void show_notice(const std::string& text);
bool last_view(std::span<float, 3> position, std::span<float, 3> forward);
// Every extension's section that has one, in order.
std::vector<Description> describe();
bool show_message(const std::string& text, double seconds);

} // namespace ext
